// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "clangdclient.h"

#include "clangcodemodeltr.h"
#include "clangconstants.h"
#include "clangdast.h"
#include "clangdcompletion.h"
#include "clangdfindreferences.h"
#include "clangdfollowsymbol.h"
#include "clangdmemoryusagewidget.h"
#include "clangdquickfixes.h"
#include "clangdsemantichighlighting.h"
#include "clangdswitchdecldef.h"
#include "clangtextmark.h"
#include "clangutils.h"
#include "tasktimers.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <cplusplus/AST.h>
#include <cplusplus/ASTPath.h>
#include <cppeditor/compilationdb.h>
#include <cppeditor/cppcodemodelsettings.h>
#include <cppeditor/cppeditorwidget.h>
#include <cppeditor/cppmodelmanager.h>
#include <cppeditor/cpprefactoringchanges.h>
#include <cppeditor/cppsemanticinfo.h>
#include <cppeditor/cpptoolsreuse.h>
#include <cppeditor/semantichighlighter.h>
#include <languageclient/diagnosticmanager.h>
#include <languageclient/languageclienthoverhandler.h>
#include <languageclient/languageclientinterface.h>
#include <languageclient/languageclientmanager.h>
#include <languageclient/languageclientoutline.h>
#include <languageclient/languageclientsymbolsupport.h>
#include <languageclient/languageclientutils.h>
#include <languageclient/progressmanager.h>
#include <languageserverprotocol/clientcapabilities.h>
#include <languageserverprotocol/progresssupport.h>
#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/devicesupport/idevice.h>
#include <projectexplorer/devicesupport/devicekitaspects.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectnodes.h>
#include <projectexplorer/projecttree.h>
#include <projectexplorer/projectmanager.h>
#include <projectexplorer/target.h>
#include <texteditor/texteditor.h>
#include <utils/algorithm.h>
#include <utils/async.h>
#include <utils/clangutils.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/mimeconstants.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QPair>
#include <QPointer>
#include <QRegularExpression>

#include <cmath>
#include <optional>
#include <unordered_map>
#include <utility>

using namespace CPlusPlus;
using namespace Core;
using namespace LanguageClient;
using namespace LanguageServerProtocol;
using namespace ProjectExplorer;
using namespace TextEditor;
using namespace Utils;

namespace ClangCodeModel {
namespace Internal {

using Key = LanguageServerProtocol::Key;

Q_LOGGING_CATEGORY(clangdLog, "qtc.clangcodemodel.clangd", QtWarningMsg);
Q_LOGGING_CATEGORY(clangdLogAst, "qtc.clangcodemodel.clangd.ast", QtWarningMsg);
static Q_LOGGING_CATEGORY(clangdLogServer, "qtc.clangcodemodel.clangd.server", QtWarningMsg);
static QString indexingToken() { return "backgroundIndexProgress"; }

class SymbolDetails : public JsonObject
{
public:
    using JsonObject::JsonObject;

    static constexpr Key usrKey{"usr"};

    // the unqualified name of the symbol
    QString name() const { return typedValue<QString>(nameKey); }

    // the enclosing namespace, class etc (without trailing ::)
    // [NOTE: This is not true, the trailing colons are included]
    QString containerName() const { return typedValue<QString>(containerNameKey); }

    // the clang-specific “unified symbol resolution” identifier
    QString usr() const { return typedValue<QString>(usrKey); }

    // the clangd-specific opaque symbol ID
    std::optional<QString> id() const { return optionalValue<QString>(idKey); }

    bool isValid() const override
    {
        return contains(nameKey) && contains(containerNameKey) && contains(usrKey);
    }
};

class SymbolInfoRequest : public Request<LanguageClientArray<SymbolDetails>, std::nullptr_t, TextDocumentPositionParams>
{
public:
    using Request::Request;
    explicit SymbolInfoRequest(const TextDocumentPositionParams &params)
        : Request("textDocument/symbolInfo", params) {}
};

class ClangdOutlineItem : public LanguageClientOutlineItem
{
    using LanguageClientOutlineItem::LanguageClientOutlineItem;
private:
    QVariant data(int column, int role) const override
    {
        if (valid()) {
            switch (role) {
            case Qt::DisplayRole:
                return ClangdClient::displayNameFromDocumentSymbol(
                    static_cast<SymbolKind>(type()), name(), detail());
            case Qt::ForegroundRole:
                if ((detail().endsWith("class") || detail().endsWith("struct"))
                    && range().end() == selectionRange().end()) {
                    return creatorColor(Theme::TextColorDisabled);
                }
                break;
            case AnnotationRole:
                // Item details are integrated into the displayname through the DisplayRole
                return {};
            }

        }
        return LanguageClientOutlineItem::data(column, role);
    }
};

void setupClangdConfigFile()
{
    const FilePath targetConfigFile = CppEditor::ClangdSettings::clangdUserConfigFilePath();
    const FilePath baseDir = targetConfigFile.parentDir();
    baseDir.ensureWritableDir();
    const Result<QByteArray> contents = targetConfigFile.fileContents();
    const QByteArray firstLine = "# This file was generated by Qt Creator and will be overwritten "
                                 "unless you remove this line.";
    if (!contents || contents->startsWith(firstLine)) {
        FileSaver saver(targetConfigFile);
        saver.write(QByteArray(firstLine + '\n'));
        saver.write("Hover:\n");
        saver.write("  ShowAKA: Yes\n");
        saver.write("Diagnostics:\n");
        saver.write("  UnusedIncludes: Strict\n");
        QTC_CHECK(saver.finalize());
    }
}

static std::optional<Utils::FilePath> clangdExecutableFromBuildDevice(Kit *kit)
{
    if (!kit)
        return std::nullopt;

    if (const IDeviceConstPtr buildDevice = BuildDeviceKitAspect::device(kit))
        return buildDevice->clangdExecutable();

    return std::nullopt;
}

static BaseClientInterface *clientInterface(BuildConfiguration *bc, const Utils::FilePath &jsonDbDir)
{
    using CppEditor::ClangdSettings;
    QString indexingOption = "--background-index";
    const ClangdSettings settings(CppEditor::ClangdProjectSettings(bc).settings());
    const ClangdSettings::IndexingPriority indexingPriority = settings.indexingPriority();
    const bool indexingEnabled = indexingPriority != ClangdSettings::IndexingPriority::Off;
    if (!indexingEnabled)
        indexingOption += "=0";
    CppEditor::ClangdProjectSettings(bc).unblockIndexing();
    const QString headerInsertionOption = QString("--header-insertion=")
            + (settings.autoIncludeHeaders() ? "iwyu" : "never");
    const QString limitResults = QString("--limit-results=%1").arg(settings.completionResults());
    const Utils::FilePath clangdExePath = clangdExecutableFromBuildDevice(bc ? bc->kit() : nullptr).value_or(
        settings.clangdFilePath());
    Utils::CommandLine cmd{clangdExePath,
                           {indexingOption,
                            headerInsertionOption,
                            limitResults,
                            "--limit-references=0",
                            "--clang-tidy=0"}};
    if (settings.workerThreadLimit() != 0)
        cmd.addArg("-j=" + QString::number(settings.workerThreadLimit()));
    if (indexingEnabled) {
        cmd.addArg("--background-index-priority="
                   + ClangdSettings::priorityToString(indexingPriority));
    }
    cmd.addArg("--rename-file-limit=0");
    if (!jsonDbDir.isEmpty())
        cmd.addArg("--compile-commands-dir=" + clangdExePath.withNewMappedPath(jsonDbDir).path());
    if (clangdLogServer().isDebugEnabled())
        cmd.addArgs({"--log=verbose", "--pretty", "--hidden-features=1"});
    cmd.addArg("--use-dirty-headers");
    if (settings.completionRankingModel() != ClangdSettings::CompletionRankingModel::Default) {
        cmd.addArg("--ranking-model=" + ClangdSettings::rankingModelToCmdLineString(
                       settings.completionRankingModel()));
    }
    const auto interface = new StdIOClientInterface;
    interface->setCommandLine(cmd);
    interface->setAllowCoreDumps(qtcEnvironmentVariableIsSet("QTC_CLANGD_CORE_DUMPS"));
    return interface;
}

class DiagnosticsCapabilities : public JsonObject
{
public:
    using JsonObject::JsonObject;
    void enableCategorySupport() { insert(Key{"categorySupport"}, true); }
    void enableCodeActionsInline() {insert(Key{"codeActionsInline"}, true);}
};

class InactiveRegionsCapabilities : public JsonObject
{
public:
    using JsonObject::JsonObject;
    void enableInactiveRegionsSupport() { insert(Key{"inactiveRegions"}, true); }
};

class ClangdTextDocumentClientCapabilities : public TextDocumentClientCapabilities
{
public:
    using TextDocumentClientCapabilities::TextDocumentClientCapabilities;

    void setPublishDiagnostics(const DiagnosticsCapabilities &caps)
    { insert(Key{"publishDiagnostics"}, caps); }
    void setInactiveRegionsCapabilities(const InactiveRegionsCapabilities &caps)
    { insert(Key{"inactiveRegionsCapabilities"}, caps); }
};

static qint64 getRevision(const TextDocument *doc)
{
    return doc->document()->revision();
}
static qint64 getRevision(const Utils::FilePath &fp)
{
    return fp.lastModified().toMSecsSinceEpoch();
}

template<typename DocType, typename DataType> class VersionedDocData
{
public:
    VersionedDocData(const DocType &doc, const DataType &data) :
        revision(getRevision(doc)), data(data) {}

    const qint64 revision;
    const DataType data;
};

template<typename DocType, typename DataType> class VersionedDataCache
{
public:
    void insert(const DocType &doc, const DataType &data)
    {
        m_data.emplace(doc, VersionedDocData(doc, data));
    }
    void remove(const DocType &doc) { m_data.erase(doc); }
    std::optional<VersionedDocData<DocType, DataType>> take(const DocType &doc)
    {
        const auto it = m_data.find(doc);
        if (it == m_data.end())
            return {};
        const auto data = it->second;
        m_data.erase(it);
        return data;
    }
    std::optional<DataType> get(const DocType &doc)
    {
        const auto it = m_data.find(doc);
        if (it == m_data.end())
            return {};
        if (it->second.revision != getRevision(doc)) {
            m_data.erase(it);
            return {};
        }
        return it->second.data;
    }
private:
    std::unordered_map<DocType, VersionedDocData<DocType, DataType>> m_data;
};

class HighlightingData
{
public:
    // For all QPairs, the int member is the corresponding document version.
    QPair<QList<ExpandedSemanticToken>, int> previousTokens;

    // The ranges of symbols referring to virtual functions,
    // as extracted by the highlighting procedure.
    QPair<QList<Range>, int> virtualRanges;

    // The highlighter is owned by its document.
    CppEditor::SemanticHighlighter *highlighter = nullptr;
};

class ClangdClient::Private
{
public:
    Private(ClangdClient *q, BuildConfiguration *bc)
        : q(q), settings(CppEditor::ClangdProjectSettings(bc).settings())
    {}

    void findUsages(TextDocument *document, const QTextCursor &cursor,
                    const QString &searchTerm, const std::optional<QString> &replacement,
                    const std::function<void()> &callback, bool categorize);

    void handleDeclDefSwitchReplies();

    static CppEditor::CppEditorWidget *widgetFromDocument(const TextDocument *doc);
    QString searchTermFromCursor(const QTextCursor &cursor) const;
    QTextCursor adjustedCursor(const QTextCursor &cursor, const TextDocument *doc);

    void setHelpItemForTooltip(const MessageId &token,
                               const Utils::FilePath &filePath,
                               const QString &fqn = {},
                               HelpItem::Category category = HelpItem::Unknown,
                               const QString &type = {});

    void handleSemanticTokens(TextDocument *doc, const QList<ExpandedSemanticToken> &tokens,
                              int version, bool force);

    MessageId getAndHandleAst(const TextDocOrFile &doc, const AstHandler &astHandler,
                              AstCallbackMode callbackMode, const Range &range = {});

    ClangdClient * const q;
    const CppEditor::ClangdSettings::Data settings;
    QList<ClangdFollowSymbol *> followSymbolOps;
    ClangdSwitchDeclDef *switchDeclDef = nullptr;
    ClangdFindLocalReferences *findLocalRefs = nullptr;
    std::optional<QVersionNumber> versionNumber;

    QHash<TextDocument *, HighlightingData> highlightingData;
    QHash<Utils::FilePath, CppEditor::BaseEditorDocumentParser::Configuration> parserConfigs;
    QHash<Utils::FilePath, int> openedExtraFiles;

    VersionedDataCache<const TextDocument *, ClangdAstNode> astCache;
    VersionedDataCache<Utils::FilePath, ClangdAstNode> externalAstCache;
    TaskTimer highlightingTimer{"highlighting"};
    bool isFullyIndexed = false;
    bool isTesting = false;
};

static void addToCompilationDb(QJsonObject &cdb,
                               const CppEditor::ProjectPart &projectPart,
                               CppEditor::UsePrecompiledHeaders usePch,
                               const QJsonArray &projectPartOptions,
                               const Utils::FilePath &workingDir,
                               const CppEditor::ProjectFile &sourceFile,
                               bool clStyle)
{
    QJsonArray args = clangOptionsForFile(sourceFile, projectPart, projectPartOptions, usePch,
                                          clStyle);

    // TODO: clangd seems to apply some heuristics depending on what we put here.
    //       Should we make use of them or keep using our own?
    args.prepend("clang");

    const QString fileString = sourceFile.path.toUserOutput();
    args.append(fileString);
    QJsonObject value;
    value.insert("workingDirectory", workingDir.path());
    value.insert("compilationCommand", args);
    cdb.insert(fileString, value);
}

static void addCompilationDb(QJsonObject &parentObject, const QJsonObject &cdb)
{
    parentObject.insert("compilationDatabaseChanges", cdb);
}

ClangdClient::ClangdClient(BuildConfiguration *bc, const Utils::FilePath &jsonDbDir, const Id &id)
    : Client(clientInterface(bc, jsonDbDir), id), d(new Private(this, bc))
{
    setName(Tr::tr("clangd"));
    LanguageFilter langFilter;
    using namespace Utils::Constants;
    langFilter.mimeTypes = QStringList{C_HEADER_MIMETYPE, C_SOURCE_MIMETYPE,
            CPP_HEADER_MIMETYPE, CPP_SOURCE_MIMETYPE, OBJECTIVE_CPP_SOURCE_MIMETYPE,
            OBJECTIVE_C_SOURCE_MIMETYPE, CUDA_SOURCE_MIMETYPE};
    setSupportedLanguage(langFilter);
    setCompletionAssistProvider(new ClangdCompletionAssistProvider(this));
    setFunctionHintAssistProvider(new ClangdFunctionHintProvider(this));
    setQuickFixAssistProvider(new ClangdQuickFixProvider(this));
    symbolSupport().setLimitRenamingToProjects(true);
    symbolSupport().setRenameResultsEnhancer([](const SearchResultItems &symbolOccurrencesInCode) {
        return CppEditor::symbolOccurrencesInDeclarationComments(symbolOccurrencesInCode);
    });
    if (!bc) {
        QJsonObject initOptions;
        const Utils::FilePath includeDir
                = CppEditor::ClangdSettings(d->settings).clangdIncludePath();
        CppEditor::CompilerOptionsBuilder optionsBuilder = clangOptionsBuilder(
                    *CppEditor::CppModelManager::fallbackProjectPart(),
                    warningsConfigForProject(nullptr), includeDir, {});
        const CppEditor::UsePrecompiledHeaders usePch
            = CppEditor::CppCodeModelSettings::usePrecompiledHeaders(nullptr);
        const QJsonArray projectPartOptions = fullProjectPartOptions(
                    optionsBuilder, globalClangOptions());
        const QJsonArray clangOptions = clangOptionsForFile({}, optionsBuilder.projectPart(),
                                                            projectPartOptions, usePch,
                                                            optionsBuilder.isClStyle());
        initOptions.insert("fallbackFlags", clangOptions);
        setInitializationOptions(initOptions);
    }
    auto isRunningClangdClient = [](const LanguageClient::Client *c) {
        return qobject_cast<const ClangdClient *>(c) && c->state() != Client::ShutdownRequested
               && c->state() != Client::Shutdown;
    };
    const QList<Client *> clients =
            Utils::filtered(LanguageClientManager::clientsForBuildConfiguration(bc), isRunningClangdClient);
    QTC_CHECK(clients.isEmpty());
    for (const Client *client : clients)
        qCWarning(clangdLog) << client->name() << client->stateString();
    ClientCapabilities caps = Client::defaultClientCapabilities();
    std::optional<TextDocumentClientCapabilities> textCaps = caps.textDocument();
    if (textCaps) {
        ClangdTextDocumentClientCapabilities clangdTextCaps(*textCaps);
        clangdTextCaps.clearDocumentHighlight();
        DiagnosticsCapabilities diagnostics;
        diagnostics.enableCategorySupport();
        diagnostics.enableCodeActionsInline();
        clangdTextCaps.setPublishDiagnostics(diagnostics);
        InactiveRegionsCapabilities inactiveRegions;
        inactiveRegions.enableInactiveRegionsSupport();
        clangdTextCaps.setInactiveRegionsCapabilities(inactiveRegions);
        std::optional<TextDocumentClientCapabilities::CompletionCapabilities> completionCaps
                = textCaps->completion();
        if (completionCaps)
            clangdTextCaps.setCompletion(ClangdCompletionCapabilities(*completionCaps));

        // https://clangd.llvm.org/extensions#reference-container
        if (const auto references = textCaps->references()) {
            QJsonObject obj = *references;
            obj.insert("container", true);
            clangdTextCaps.setReferences(DynamicRegistrationCapabilities(obj));
        }

        caps.setTextDocument(clangdTextCaps);
    }
    caps.clearExperimental();
    setClientCapabilities(caps);
    setLocatorsEnabled(false);
    setAutoRequestCodeActions(false); // clangd sends code actions inside diagnostics
    progressManager()->setTitleForToken(indexingToken(),
            bc ? Tr::tr("Indexing %1 with clangd").arg(bc->project()->displayName())
               : Tr::tr("Indexing session with clangd"));
    progressManager()->setCancelHandlerForToken(indexingToken(), [this, bc = QPointer(bc)] {
        if (!bc)
            return;
        CppEditor::ClangdProjectSettings projectSettings(bc);
        projectSettings.blockIndexing();
        progressManager()->endProgressReport(indexingToken());
    });
    setCurrentBuildConfiguration(bc);
    setDocumentChangeUpdateThreshold(d->settings.documentUpdateThreshold);
    setSemanticTokensHandler([this](TextDocument *doc, const QList<ExpandedSemanticToken> &tokens,
                                    int version, bool force) {
        d->handleSemanticTokens(doc, tokens, version, force);
    });
    hoverHandler()->setHelpItemProvider([this](const HoverRequest::Response &response,
                                               const Utils::FilePath &filePath) {
        gatherHelpItemForTooltip(response, filePath);
    });
    registerCustomMethod(inactiveRegionsMethodName(), [this](const JsonRpcMessage &msg) {
        handleInactiveRegions(this, msg);
        return true;
    });

    connect(this, &Client::workDone, this,
            [this, bc = QPointer(bc)](const ProgressToken &token) {
        const QString * const val = std::get_if<QString>(&token);
        if (val && *val == indexingToken()) {
            d->isFullyIndexed = true;
            emit indexingFinished();
#ifdef WITH_TESTS
            if (bc)
                emit bc->project()->indexingFinished("Indexer.Clangd");
#endif
        }
    });

    connect(this, &Client::initialized, this, [this] { d->openedExtraFiles.clear(); });

    start();
}

ClangdClient::~ClangdClient()
{
    for (ClangdFollowSymbol * const followSymbol : std::as_const(d->followSymbolOps))
        followSymbol->clear();
    delete d;
}

bool ClangdClient::isFullyIndexed() const
{
    return d->isFullyIndexed;
}

void ClangdClient::openExtraFile(const Utils::FilePath &filePath, const QString &content)
{
    const auto it = d->openedExtraFiles.find(filePath);
    if (it != d->openedExtraFiles.end()) {
        QTC_CHECK(it.value() > 0);
        ++it.value();
        return;
    }

    QString text;
    if (!content.isEmpty()) {
        text = content;
    } else {
        Result<QByteArray> fileContent = filePath.fileContents();
        if (!fileContent)
            return;
        text = QString::fromUtf8(*std::move(fileContent));
    }

    TextDocumentItem item;
    item.setLanguageId("cpp");
    item.setUri(hostPathToServerUri(filePath));
    item.setText(std::move(text));
    item.setVersion(0);
    sendMessage(DidOpenTextDocumentNotification(DidOpenTextDocumentParams(item)),
                SendDocUpdates::Ignore);

    d->openedExtraFiles.insert(filePath, 1);
}

void ClangdClient::closeExtraFile(const Utils::FilePath &filePath)
{
    const auto it = d->openedExtraFiles.find(filePath);
    QTC_ASSERT(it != d->openedExtraFiles.end(), return);
    QTC_CHECK(it.value() > 0);
    if (--it.value() > 0)
        return;
    d->openedExtraFiles.erase(it);
    sendMessage(DidCloseTextDocumentNotification(DidCloseTextDocumentParams(
            TextDocumentIdentifier{hostPathToServerUri(filePath)})),
                SendDocUpdates::Ignore);
}

void ClangdClient::findUsages(const CppEditor::CursorInEditor &cursor,
                              const std::optional<QString> &replacement,
                              const std::function<void()> &renameCallback)
{
    // Quick check: Are we even on anything searchable?
    const QTextCursor adjustedCursor = d->adjustedCursor(cursor.cursor(), cursor.textDocument());
    const QString searchTerm = d->searchTermFromCursor(adjustedCursor);
    if (searchTerm.isEmpty())
        return;

    if (replacement && Utils::qtcEnvironmentVariable("QTC_CLANGD_RENAMING") != "0") {
        // If we have up-to-date highlighting data, we can prevent giving clangd
        // macros or namespaces to rename, which it can't cope with.
        // TODO: Fix this upstream for macros; see https://github.com/clangd/clangd/issues/729.
        bool useClangdForRenaming = true;
        const auto highlightingData = d->highlightingData.constFind(cursor.textDocument());
        if (highlightingData != d->highlightingData.end()
            && highlightingData->previousTokens.second == documentVersion(cursor.filePath())) {
            const auto candidate = std::lower_bound(
                highlightingData->previousTokens.first.cbegin(),
                highlightingData->previousTokens.first.cend(),
                cursor.cursor().position(),
                [&cursor](const ExpandedSemanticToken &token, int pos) {
                    const int startPos = Utils::Text::positionInText(
                        cursor.textDocument()->document(), token.line, token.column - 1);
                    return startPos + token.length < pos;
                });
            if (candidate != highlightingData->previousTokens.first.cend()) {
                const int startPos = Utils::Text::positionInText(
                    cursor.textDocument()->document(), candidate->line, candidate->column - 1);
                if (startPos <= cursor.cursor().position()) {
                    if (candidate->type == "namespace") {
                        CppEditor::CppModelManager::globalRename(
                            cursor, *replacement, renameCallback,
                            CppEditor::CppModelManager::Backend::Builtin);
                        return;
                    }
                    if (candidate->type == "macro")
                        useClangdForRenaming = false;
                }
            }
        }

        if (useClangdForRenaming) {
            symbolSupport().renameSymbol(cursor.textDocument(), adjustedCursor, *replacement,
                                         renameCallback,
                                         CppEditor::preferLowerCaseFileNames(project()));
            return;
        }
    }

    const bool categorize = CppEditor::CppCodeModelSettings::categorizeFindReferences();

    // If it's a "normal" symbol, go right ahead.
    if (searchTerm != "operator" && Utils::allOf(searchTerm, [](const QChar &c) {
            return c.isLetterOrNumber() || c == '_';
    })) {
        d->findUsages(cursor.textDocument(), adjustedCursor, searchTerm, replacement,
                      renameCallback, categorize);
        return;
    }

    // Otherwise get the proper spelling of the search term from clang, so we can put it into the
    // search widget.
    const auto symbolInfoHandler = [this, doc = QPointer(cursor.textDocument()), adjustedCursor,
                                    replacement, renameCallback, categorize]
            (const QString &name, const QString &, const MessageId &) {
        if (!doc)
            return;
        if (name.isEmpty())
            return;
        d->findUsages(doc.data(), adjustedCursor, name, replacement, renameCallback, categorize);
    };
    requestSymbolInfo(cursor.textDocument()->filePath(), Range(adjustedCursor).start(),
                      symbolInfoHandler);
}

void ClangdClient::checkUnused(const Utils::Link &link, Core::SearchResult *search,
                               const Utils::LinkHandler &callback)
{
    new ClangdFindReferences(this, link, search, callback);
}

void ClangdClient::handleDiagnostics(const PublishDiagnosticsParams &params)
{
    const DocumentUri &uri = params.uri();
    Client::handleDiagnostics(params);
    const int docVersion = documentVersion(uri);
    if (params.version().value_or(docVersion) != docVersion)
        return;
    QList<CodeAction> allCodeActions;
    for (const Diagnostic &diagnostic : params.diagnostics()) {
        const ClangdDiagnostic clangdDiagnostic(diagnostic);
        auto codeActions = clangdDiagnostic.codeActions();
        if (codeActions && !codeActions->isEmpty()) {
            for (CodeAction &action : *codeActions)
                action.setDiagnostics({diagnostic});
            allCodeActions << *codeActions;
        } else {
            // We know that there's only one kind of diagnostic for which clangd has
            // a quickfix tweak, so let's not be wasteful.
            const Diagnostic::Code code = diagnostic.code().value_or(Diagnostic::Code());
            const QString * const codeString = std::get_if<QString>(&code);
            if (codeString && *codeString == "-Wswitch")
                requestCodeActions(uri, diagnostic);
        }
    }
    if (!allCodeActions.isEmpty())
        LanguageClient::updateCodeActionRefactoringMarker(this, allCodeActions, uri);
}

void ClangdClient::handleDocumentOpened(TextDocument *doc)
{
    const auto data = d->externalAstCache.take(doc->filePath());
    if (!data)
        return;
    if (data->revision == getRevision(doc->filePath()))
       d->astCache.insert(doc, data->data);
}

void ClangdClient::handleDocumentClosed(TextDocument *doc)
{
    d->highlightingData.remove(doc);
    d->astCache.remove(doc);
    d->parserConfigs.remove(doc->filePath());
}

QTextCursor ClangdClient::adjustedCursorForHighlighting(const QTextCursor &cursor,
                                                        TextEditor::TextDocument *doc)
{
    return d->adjustedCursor(cursor, doc);
}

const LanguageClient::Client::CustomInspectorTabs ClangdClient::createCustomInspectorTabs()
{
    return {{new ClangdMemoryUsageWidget(this), Tr::tr("Memory Usage")}};
}

class ClangdDiagnosticManager : public LanguageClient::DiagnosticManager
{
public:
    ClangdDiagnosticManager(LanguageClient::Client *client)
        : LanguageClient::DiagnosticManager(client)
    {
        setTaskCategory(Constants::TASK_CATEGORY_DIAGNOSTICS);
        setForceCreateTasks(false);
    }
private:

    QList<Diagnostic> filteredDiagnostics(const QList<Diagnostic> &diagnostics) const override
    {
        return Utils::filtered(diagnostics, [](const Diagnostic &diag){
            const Diagnostic::Code code = diag.code().value_or(Diagnostic::Code());
            const QString * const codeString = std::get_if<QString>(&code);
            return !codeString || (*codeString != "drv_unknown_argument"
                                   && !codeString->startsWith("drv_unsupported_opt"));
        });
    }

    TextMark *createTextMark(TextDocument *doc,
                             const Diagnostic &diagnostic,
                             bool isProjectFile) const override
    {
        return new ClangdTextMark(
                    doc, diagnostic, isProjectFile, qobject_cast<ClangdClient *>(client()));
    }

    QString taskText(const Diagnostic &diagnostic) const override
    {
        QString text = diagnostic.message();
        auto splitIndex = text.indexOf("\n\n");
        if (splitIndex >= 0)
            text.truncate(splitIndex);

        return diagnosticCategoryPrefixRemoved(text);
    }
};

DiagnosticManager *ClangdClient::createDiagnosticManager()
{
    auto diagnosticManager = new ClangdDiagnosticManager(this);
    if (d->isTesting) {
        connect(diagnosticManager, &DiagnosticManager::textMarkCreated,
                this, &ClangdClient::textMarkCreated);
    }
    return diagnosticManager;
}

LanguageClientOutlineItem *ClangdClient::createOutlineItem(
    const LanguageServerProtocol::DocumentSymbol &symbol)
{
    return new ClangdOutlineItem(this, symbol);
}

bool ClangdClient::referencesShadowFile(const TextEditor::TextDocument *doc,
                                        const Utils::FilePath &candidate)
{
    const QRegularExpression includeRex("#include.*" + candidate.fileName() + R"([>"])");
    const QTextCursor includePos = doc->document()->find(includeRex);
    return !includePos.isNull();
}

bool ClangdClient::fileBelongsToProject(const Utils::FilePath &filePath) const
{
    if (CppEditor::ClangdSettings::instance().granularity()
            == CppEditor::ClangdSettings::Granularity::Session) {
        return ProjectManager::projectForFile(filePath);
    }
    return Client::fileBelongsToProject(filePath);
}

QList<Text::Range> ClangdClient::additionalDocumentHighlights(
    TextEditorWidget *editorWidget, const QTextCursor &cursor)
{
    return CppEditor::symbolOccurrencesInDeclarationComments(
        qobject_cast<CppEditor::CppEditorWidget *>(editorWidget), cursor);
}

bool ClangdClient::shouldSendDidSave(const TextEditor::TextDocument *doc) const
{
    for (const Project * const p : ProjectManager::projects()) {
        if (const Node * const n  = p->nodeForFilePath(doc->filePath()))
            return n->asFileNode() && n->asFileNode()->fileType() == FileType::Header;
    }
    return CppEditor::ProjectFile::isHeader(doc->filePath());
}

RefactoringFilePtr ClangdClient::createRefactoringFile(const FilePath &filePath) const
{
    return CppEditor::CppRefactoringChanges(CppEditor::CppModelManager::snapshot()).file(filePath);
}

QVersionNumber ClangdClient::versionNumber() const
{
    if (d->versionNumber)
        return d->versionNumber.value();

    static const QRegularExpression versionPattern("^clangd version (\\d+)\\.(\\d+)\\.(\\d+).*$");
    QTC_CHECK(versionPattern.isValid());
    const QRegularExpressionMatch match = versionPattern.match(serverVersion());
    if (match.isValid()) {
        d->versionNumber.emplace({match.captured(1).toInt(), match.captured(2).toInt(),
                                 match.captured(3).toInt()});
    } else {
        qCWarning(clangdLog) << "Failed to parse clangd server string" << serverVersion();
        d->versionNumber.emplace({0});
    }
    return d->versionNumber.value();
}

CppEditor::ClangdSettings::Data ClangdClient::settingsData() const { return d->settings; }

void ClangdClient::Private::findUsages(TextDocument *document,
        const QTextCursor &cursor, const QString &searchTerm,
        const std::optional<QString> &replacement, const std::function<void()> &renameCallback,
        bool categorize)
{
    const auto findRefs = new ClangdFindReferences(q, document, cursor, searchTerm, replacement,
                                                   renameCallback, categorize);
    if (isTesting) {
        connect(findRefs, &ClangdFindReferences::foundReferences,
                q, &ClangdClient::foundReferences);
        connect(findRefs, &ClangdFindReferences::done, q, &ClangdClient::findUsagesDone);
    }
}

void ClangdClient::enableTesting()
{
    d->isTesting = true;
}

bool ClangdClient::testingEnabled() const
{
    return d->isTesting;
}

QString ClangdClient::displayNameFromDocumentSymbol(SymbolKind kind, const QString &name,
                                                    const QString &detail)
{
    switch (kind) {
    case SymbolKind::Constructor:
        return name + detail;
    case SymbolKind::Method:
    case SymbolKind::Function: {
        const int lastParenOffset = detail.lastIndexOf(')');
        if (lastParenOffset == -1)
            return name;
        int leftParensNeeded = 1;
        int i = -1;
        for (i = lastParenOffset - 1; i >= 0; --i) {
            switch (detail.at(i).toLatin1()) {
            case ')':
                ++leftParensNeeded;
                break;
            case '(':
                --leftParensNeeded;
                break;
            default:
                break;
            }
            if (leftParensNeeded == 0)
                break;
        }
        if (leftParensNeeded > 0)
            return name;
        return name + detail.mid(i) + " -> " + detail.left(i);
    }
    case SymbolKind::Variable:
    case SymbolKind::Field:
    case SymbolKind::Constant:
        if (detail.isEmpty())
            return name;
        return name + " -> " + detail;
    default:
        return name;
    }
}

// Force re-parse of all open files that include the changed ui header.
// Otherwise, we potentially have stale diagnostics.
void ClangdClient::handleUiHeaderChange(const QString &fileName)
{
    const QRegularExpression includeRex("#include.*" + fileName + R"([>"])");
    const QList<Client *> &allClients = LanguageClientManager::clients();
    for (Client * const client : allClients) {
        if (!client->reachable() || !qobject_cast<ClangdClient *>(client))
            continue;
        for (IDocument * const doc : DocumentModel::openedDocuments()) {
            const auto textDoc = qobject_cast<TextDocument *>(doc);
            if (!textDoc || !client->documentOpen(textDoc))
                continue;
            const QTextCursor includePos = textDoc->document()->find(includeRex);
            if (includePos.isNull())
                continue;
            qCDebug(clangdLog) << "updating" << textDoc->filePath() << "due to change in UI header"
                               << fileName;
            client->documentContentsChanged(textDoc, 0, 0, 0);
            break; // No sane project includes the same UI header twice.
        }
    }
}

void ClangdClient::updateParserConfig(const Utils::FilePath &filePath,
        const CppEditor::BaseEditorDocumentParser::Configuration &config)
{
    // TODO: Also handle usePrecompiledHeaders?
    // TODO: Should we write the editor defines into the json file? It seems strange
    //       that they should affect the index only while the file is open in the editor.
    const auto projectPart = !config.preferredProjectPartId.isEmpty()
            ? CppEditor::CppModelManager::projectPartForId(config.preferredProjectPartId)
            : projectPartForFile(filePath);
    if (!projectPart)
        return;

    CppEditor::BaseEditorDocumentParser::Configuration fullConfig = config;
    fullConfig.preferredProjectPartId = projectPart->id();
    auto cachedConfig = d->parserConfigs.find(filePath);
    if (cachedConfig == d->parserConfigs.end()) {
        cachedConfig = d->parserConfigs.insert(filePath, fullConfig);
        if (config.preferredProjectPartId.isEmpty() && config.editorDefines.isEmpty())
            return;
    } else if (cachedConfig.value() == fullConfig) {
        return;
    }
    cachedConfig.value() = fullConfig;

    QJsonObject cdbChanges;
    const Utils::FilePath includeDir = CppEditor::ClangdSettings(d->settings).clangdIncludePath();
    CppEditor::CompilerOptionsBuilder optionsBuilder = clangOptionsBuilder(
                *projectPart, warningsConfigForProject(project()), includeDir,
                ProjectExplorer::Macro::toMacros(config.editorDefines));
    const CppEditor::ProjectFile file(filePath, CppEditor::ProjectFile::classify(filePath));
    const QJsonArray projectPartOptions = fullProjectPartOptions(
                optionsBuilder, globalClangOptions());
    const auto cppSettings = CppEditor::CppCodeModelSettings::settingsForProject(
        projectPart->topLevelProject);
    addToCompilationDb(cdbChanges,
                       *projectPart,
                       cppSettings.usePrecompiledHeaders(),
                       projectPartOptions,
                       filePath.parentDir(),
                       file,
                       optionsBuilder.isClStyle());
    QJsonObject settings;
    addCompilationDb(settings, cdbChanges);
    DidChangeConfigurationParams configChangeParams;
    configChangeParams.setSettings(settings);
    sendMessage(DidChangeConfigurationNotification(configChangeParams));
    emit configChanged();
}

std::optional<bool> ClangdClient::hasVirtualFunctionAt(TextDocument *doc, int revision,
                                                         const Range &range)
{
    const auto highlightingData = d->highlightingData.constFind(doc);
    if (highlightingData == d->highlightingData.constEnd()
            || highlightingData->virtualRanges.second != revision) {
        return {};
    }
    const auto matcher = [range](const Range &r) { return range.overlaps(r); };
    return Utils::contains(highlightingData->virtualRanges.first, matcher);
}

MessageId ClangdClient::getAndHandleAst(const TextDocOrFile &doc, const AstHandler &astHandler,
                                        AstCallbackMode callbackMode, const Range &range)
{
    return d->getAndHandleAst(doc, astHandler, callbackMode, range);
}

MessageId ClangdClient::requestSymbolInfo(const Utils::FilePath &filePath, const Position &position,
                                          const SymbolInfoHandler &handler)
{
    const TextDocumentIdentifier docId(hostPathToServerUri(filePath));
    const TextDocumentPositionParams params(docId, position);
    SymbolInfoRequest symReq(params);
    symReq.setResponseCallback([handler, reqId = symReq.id()]
                               (const SymbolInfoRequest::Response &response) {
        const auto result = response.result();
        if (!result) {
            handler({}, {}, reqId);
            return;
        }

        // According to the documentation, we should receive a single
        // object here, but it's a list. No idea what it means if there's
        // more than one entry. We choose the first one.
        const auto list = std::get_if<QList<SymbolDetails>>(&(*result));
        if (!list || list->isEmpty()) {
            handler({}, {}, reqId);
            return;
        }

        const SymbolDetails &sd = list->first();
        handler(sd.name(), sd.containerName(), reqId);
    });
    sendMessage(symReq);
    return symReq.id();
}

#ifdef WITH_TESTS
ClangdFollowSymbol *ClangdClient::currentFollowSymbolOperation()
{
    return d->followSymbolOps.isEmpty() ? nullptr : d->followSymbolOps.first();
}
#endif

void ClangdClient::followSymbol(TextDocument *document,
        const QTextCursor &cursor,
        CppEditor::CppEditorWidget *editorWidget,
        const Utils::LinkHandler &callback,
        bool resolveTarget,
        FollowTo followTo,
        bool openInSplit
        )
{
    QTC_ASSERT(documentOpen(document), openDocument(document));

    const ClangdFollowSymbol::Origin origin
        = CppEditor::CppCodeModelSettings::isInteractiveFollowSymbol()
              ? ClangdFollowSymbol::Origin::User
              : ClangdFollowSymbol::Origin::Code;
    if (origin == ClangdFollowSymbol::Origin::User) {
        for (auto it = d->followSymbolOps.begin(); it != d->followSymbolOps.end(); ) {
            ClangdFollowSymbol * const followSymbol = *it;
            if (followSymbol->isInteractive()) {
                it = d->followSymbolOps.erase(it);
                followSymbol->cancel();
            } else {
                ++it;
            }
        }
    }

    const QTextCursor adjustedCursor = d->adjustedCursor(cursor, document);
    if (followTo == FollowTo::SymbolDef && !resolveTarget) {
        symbolSupport().findLinkAt(document,
                                   adjustedCursor,
                                   callback,
                                   false,
                                   LanguageClient::LinkTarget::SymbolDef);
        return;
    }

    qCDebug(clangdLog) << "follow symbol requested" << document->filePath()
                       << adjustedCursor.blockNumber() << adjustedCursor.positionInBlock();
    auto clangdFollowSymbol = new ClangdFollowSymbol(this, origin, adjustedCursor, editorWidget,
                                                     document, callback, followTo, openInSplit);
    connect(clangdFollowSymbol, &ClangdFollowSymbol::done, this, [this, clangdFollowSymbol] {
        clangdFollowSymbol->deleteLater();
        d->followSymbolOps.removeOne(clangdFollowSymbol);
    });
    d->followSymbolOps << clangdFollowSymbol;
}

void ClangdClient::switchDeclDef(TextDocument *document, const QTextCursor &cursor,
                                 CppEditor::CppEditorWidget *editorWidget,
                                 const Utils::LinkHandler &callback)
{
    QTC_ASSERT(documentOpen(document), openDocument(document));

    qCDebug(clangdLog) << "switch decl/dev requested" << document->filePath()
                       << cursor.blockNumber() << cursor.positionInBlock();
    if (d->switchDeclDef)
        delete d->switchDeclDef;
    d->switchDeclDef = new ClangdSwitchDeclDef(this, document, cursor, editorWidget, callback);
    connect(d->switchDeclDef, &ClangdSwitchDeclDef::done, this, [this] {
        d->switchDeclDef->deleteLater();
        d->switchDeclDef = nullptr;
    });
}

void ClangdClient::switchHeaderSource(const Utils::FilePath &filePath, bool inNextSplit)
{
    class SwitchSourceHeaderRequest : public Request<QJsonValue, std::nullptr_t, TextDocumentIdentifier>
    {
    public:
        using Request::Request;
        explicit SwitchSourceHeaderRequest(const DocumentUri &uri)
            : Request("textDocument/switchSourceHeader", TextDocumentIdentifier(uri))
        {}
    };
    SwitchSourceHeaderRequest req(hostPathToServerUri(filePath));
    req.setResponseCallback([inNextSplit, pathMapper = hostPathMapper()](
                                const SwitchSourceHeaderRequest::Response &response) {
        if (const std::optional<QJsonValue> result = response.result()) {
            const DocumentUri uri = DocumentUri::fromProtocol(result->toString());
            const Utils::FilePath filePath = uri.toFilePath(pathMapper);
            if (!filePath.isEmpty())
                CppEditor::openEditor(filePath, inNextSplit);
        }
    });
    sendMessage(req);
}

void ClangdClient::findLocalUsages(CppEditor::CppEditorWidget *editorWidget,
                                   const QTextCursor &cursor, CppEditor::RenameCallback &&callback)
{
    QTC_ASSERT(editorWidget, return);
    TextDocument * const document = editorWidget->textDocument();
    QTC_ASSERT(documentOpen(document), openDocument(document));

    qCDebug(clangdLog) << "local references requested" << document->filePath()
                       << (cursor.blockNumber() + 1) << (cursor.positionInBlock() + 1);

    if (d->findLocalRefs) {
        d->findLocalRefs->disconnect(this);
        d->findLocalRefs->deleteLater();
        d->findLocalRefs = nullptr;
    }

    const QString searchTerm = d->searchTermFromCursor(cursor);
    if (searchTerm.isEmpty()) {
        callback({}, {}, document->document()->revision());
        return;
    }

    d->findLocalRefs = new ClangdFindLocalReferences(this, editorWidget, cursor, callback);
    connect(d->findLocalRefs, &ClangdFindLocalReferences::done, this, [this] {
        d->findLocalRefs->deleteLater();
        d->findLocalRefs = nullptr;
    });
}

void ClangdClient::gatherHelpItemForTooltip(const HoverRequest::Response &hoverResponse,
                                            const Utils::FilePath &filePath)
{
    if (const std::optional<HoverResult> result = hoverResponse.result()) {
        if (auto hover = std::get_if<Hover>(&(*result))) {
            const HoverContent content = hover->content();
            const MarkupContent *const markup = std::get_if<MarkupContent>(&content);
            if (markup) {
                const QString markupString = markup->content();

                // Macros aren't locatable via the AST, so parse the formatted string.
                static const QString magicMacroPrefix = "### macro `";
                if (markupString.startsWith(magicMacroPrefix)) {
                    const int nameStart = magicMacroPrefix.length();
                    const int closingQuoteIndex = markupString.indexOf('`', nameStart);
                    if (closingQuoteIndex != -1) {
                        const QString macroName = markupString.mid(nameStart,
                                                                   closingQuoteIndex - nameStart);
                        d->setHelpItemForTooltip(hoverResponse.id(),
                                                 filePath,
                                                 macroName,
                                                 HelpItem::Macro);
                        return;
                    }
                }

                // Is it the file path for an include directive?
                QString cleanString = markupString;
                cleanString.remove('`');
                const QStringList lines = cleanString.trimmed().split('\n');
                for (const QString &line : lines) {
                    const QString possibleFilePath = line.simplified();
                    const auto looksLikeFilePath = [&] {
                        if (possibleFilePath.length() < 4)
                            return false;
                        if (osType() == OsTypeWindows) {
                            if (possibleFilePath.startsWith(R"(\\\\)"))
                                return true;
                            return possibleFilePath.front().isLetter()
                                    && possibleFilePath.at(1) == ':'
                                    && possibleFilePath.at(2) == '\\'
                                    && possibleFilePath.at(3) == '\\';
                        }
                        return possibleFilePath.front() == '/'
                                && possibleFilePath.at(1).isLetterOrNumber();
                    };
                    if (!looksLikeFilePath())
                        continue;
                    const auto markupFilePath = Utils::FilePath::fromUserInput(possibleFilePath);
                    if (markupFilePath.exists()) {
                        d->setHelpItemForTooltip(hoverResponse.id(),
                                                 filePath,
                                                 markupFilePath.fileName(),
                                                 HelpItem::Brief);
                        return;
                    }
                }
            }
        }
    }

    const TextDocument * const doc = documentForFilePath(filePath);
    QTC_ASSERT(doc, return);
    const auto astHandler = [this, filePath, hoverResponse](const ClangdAstNode &ast,
                                                            const MessageId &) {
        const MessageId id = hoverResponse.id();
        Range range;
        if (const std::optional<HoverResult> result = hoverResponse.result()) {
            if (auto hover = std::get_if<Hover>(&(*result)))
                range = hover->range().value_or(Range());
        }
        const ClangdAstPath path = getAstPath(ast, range);
        if (path.isEmpty()) {
            d->setHelpItemForTooltip(id, filePath);
            return;
        }
        ClangdAstNode node = path.last();
        if (node.role() == "expression" && node.kind() == "ImplicitCast") {
            const std::optional<QList<ClangdAstNode>> children = node.children();
            if (children && !children->isEmpty())
                node = children->first();
        }
        while (node.kind() == "Qualified") {
            const std::optional<QList<ClangdAstNode>> children = node.children();
            if (children && !children->isEmpty())
                node = children->first();
        }
        if (clangdLogAst().isDebugEnabled())
            node.print(0);

        QString type = node.type();
        const auto stripTemplatePartOffType = [&type] {
            const int angleBracketIndex = type.indexOf('<');
            if (angleBracketIndex != -1)
                type = type.left(angleBracketIndex);
        };

        if (gatherMemberFunctionOverrideHelpItemForTooltip(id, filePath, path))
            return;

        const bool isMemberFunction = node.role() == "expression" && node.kind() == "Member"
                && (node.arcanaContains("member function") || type.contains('('));
        const bool isFunction = node.role() == "expression" && node.kind() == "DeclRef"
                && type.contains('(');
        if (isMemberFunction || isFunction) {
            const auto symbolInfoHandler = [this, id, filePath, type, isFunction]
                    (const QString &name, const QString &prefix, const MessageId &) {
                qCDebug(clangdLog) << "handling symbol info reply";
                const QString fqn = prefix + name;

                // Unfortunately, the arcana string contains the signature only for
                // free functions, so we can't distinguish member function overloads.
                // But since HtmlDocExtractor::getFunctionDescription() is always called
                // with mainOverload = true, such information would get ignored anyway.
                if (!fqn.isEmpty())
                    d->setHelpItemForTooltip(id,
                                             filePath,
                                             fqn,
                                             HelpItem::Function,
                                             isFunction ? type : "()");
            };
            requestSymbolInfo(filePath, range.start(), symbolInfoHandler);
            return;
        }
        if ((node.role() == "expression" && node.kind() == "DeclRef")
                || (node.role() == "declaration"
                    && (node.kind() == "Var" || node.kind() == "ParmVar"
                        || node.kind() == "Field"))) {
            if (node.arcanaContains("EnumConstant")) {
                d->setHelpItemForTooltip(id,
                                         filePath,
                                         node.detail().value_or(QString()),
                                         HelpItem::Enum,
                                         type);
                return;
            }
            stripTemplatePartOffType();
            type.remove("&").remove("*").remove("const ").remove(" const")
                    .remove("volatile ").remove(" volatile");
            type = type.simplified();
            if (type != "int" && !type.contains(" int")
                    && type != "char" && !type.contains(" char")
                    && type != "double" && !type.contains(" double")
                    && type != "float" && type != "bool") {
                d->setHelpItemForTooltip(id,
                                         filePath,
                                         type,
                                         node.qdocCategoryForDeclaration(
                                             HelpItem::ClassOrNamespace));
            } else {
                d->setHelpItemForTooltip(id, filePath);
            }
            return;
        }
        if (node.isNamespace()) {
            QString ns = node.detail().value_or(QString());
            for (auto it = path.rbegin() + 1; it != path.rend(); ++it) {
                if (it->isNamespace()) {
                    const QString name = it->detail().value_or(QString());
                    if (!name.isEmpty())
                        ns.prepend("::").prepend(name);
                }
            }
            d->setHelpItemForTooltip(id, filePath, ns, HelpItem::ClassOrNamespace);
            return;
        }
        if (node.role() == "type") {
            if (node.kind() == "Enum") {
                d->setHelpItemForTooltip(id,
                                         filePath,
                                         node.detail().value_or(QString()),
                                         HelpItem::Enum);
            } else if (node.kind() == "Record" || node.kind() == "TemplateSpecialization") {
                stripTemplatePartOffType();
                d->setHelpItemForTooltip(id, filePath, type, HelpItem::ClassOrNamespace);
            } else if (node.kind() == "Typedef") {
                d->setHelpItemForTooltip(id, filePath, type, HelpItem::Typedef);
            } else {
                d->setHelpItemForTooltip(id, filePath);
            }
            return;
        }
        if (node.role() == "expression" && node.kind() == "CXXConstruct") {
            const QString name = node.detail().value_or(QString());
            if (!name.isEmpty())
                type = name;
            d->setHelpItemForTooltip(id, filePath, type, HelpItem::ClassOrNamespace);
        }
        if (node.role() == "specifier"
            && (node.kind() == "NamespaceAlias" || node.kind() == "Namespace")) {
            d->setHelpItemForTooltip(id,
                                     filePath,
                                     node.detail().value_or(QString()).chopped(2),
                                     HelpItem::ClassOrNamespace);
            return;
        }
        d->setHelpItemForTooltip(id, filePath);
    };
    d->getAndHandleAst(doc, astHandler, AstCallbackMode::SyncIfPossible);
}

bool ClangdClient::gatherMemberFunctionOverrideHelpItemForTooltip(
    const LanguageServerProtocol::MessageId &token,
    const Utils::FilePath &filePath,
    const QList<ClangdAstNode> &path)
{
    // Heuristic: If we encounter a member function re-declaration, continue under the
    // assumption that the base class holds the documentation.
    if (path.length() < 3 || path.last().kind() != "FunctionProto")
        return false;
    const ClangdAstNode &methodNode = path.at(path.length() - 2);
    if (methodNode.kind() != "CXXMethod" || !methodNode.detail())
        return false;
    bool hasOverride = false;
    for (const ClangdAstNode &methodNodeChild
         : methodNode.children().value_or(QList<ClangdAstNode>())) {
        if (methodNodeChild.kind() == "Override") {
            hasOverride = true;
            break;
        }
    }
    if (!hasOverride)
        return false;
    const ClangdAstNode &classNode = path.at(path.length() - 3);
    if (classNode.kind() != "CXXRecord")
        return false;
    const ClangdAstNode baseNode = classNode.children()->first();
    if (baseNode.role() != "base")
        return false;
    const auto baseNodeChildren = baseNode.children();
    if (!baseNodeChildren || baseNodeChildren->isEmpty())
        return false;
    const ClangdAstNode baseTypeNode = baseNodeChildren->first();
    if (baseTypeNode.role() != "type")
        return false;
    const auto baseTypeNodeChildren = baseTypeNode.children();
    if (!baseTypeNodeChildren || baseTypeNodeChildren->isEmpty())
        return false;
    const ClangdAstNode baseClassNode = baseTypeNodeChildren->first();
    if (!baseClassNode.detail())
        return false;
    d->setHelpItemForTooltip(token,
                             filePath,
                             *baseClassNode.detail() + "::" + *methodNode.detail(),
                             HelpItem::Function,
                             "()");
    return true;
}

void ClangdClient::setVirtualRanges(const Utils::FilePath &filePath, const QList<Range> &ranges,
                                    int revision)
{
    TextDocument * const doc = documentForFilePath(filePath);
    if (doc && doc->document()->revision() == revision)
        d->highlightingData[doc].virtualRanges = {ranges, revision};
}

CppEditor::CppEditorWidget *ClangdClient::Private::widgetFromDocument(const TextDocument *doc)
{
    IEditor * const editor = Utils::findOrDefault(EditorManager::visibleEditors(),
            [doc](const IEditor *editor) { return editor->document() == doc; });
    return qobject_cast<CppEditor::CppEditorWidget *>(TextEditorWidget::fromEditor(editor));
}

QString ClangdClient::Private::searchTermFromCursor(const QTextCursor &cursor) const
{
    QTextCursor termCursor(cursor);
    termCursor.select(QTextCursor::WordUnderCursor);
    return termCursor.selectedText();
}

// https://github.com/clangd/clangd/issues/936
QTextCursor ClangdClient::Private::adjustedCursor(const QTextCursor &cursor,
                                                  const TextDocument *doc)
{
    CppEditor::CppEditorWidget * const widget = widgetFromDocument(doc);
    if (!widget)
        return cursor;
    const Document::Ptr cppDoc = widget->semanticInfo().doc;
    if (!cppDoc)
        return cursor;
    const QList<AST *> builtinAstPath = ASTPath(cppDoc)(cursor);
    if (builtinAstPath.isEmpty())
        return cursor;
    const TranslationUnit * const tu = cppDoc->translationUnit();
    const auto posForToken = [doc, tu](int tok) {
        return tu->getTokenPositionInDocument(tok, doc->document());
    };
    const auto endPosForToken = [doc, tu](int tok) {
        return tu->getTokenEndPositionInDocument(tok, doc->document());
    };
    const auto leftMovedCursor = [cursor] {
        QTextCursor c = cursor;
        c.setPosition(cursor.position() - 1);
        return c;
    };

    // enum E { v1|, v2 };
    if (const EnumeratorAST * const enumAst = builtinAstPath.last()->asEnumerator()) {
        if (endPosForToken(enumAst->identifier_token) == cursor.position())
            return leftMovedCursor();
        return cursor;
    }

    for (auto it = builtinAstPath.rbegin(); it != builtinAstPath.rend(); ++it) {

        // s|.x or s|->x
        if (const MemberAccessAST * const memberAccess = (*it)->asMemberAccess()) {
            switch (tu->tokenAt(memberAccess->access_token).kind()) {
            case T_DOT:
                break;
            case T_ARROW: {
                const std::optional<ClangdAstNode> clangdAst = astCache.get(doc);
                if (!clangdAst)
                    return cursor;
                const ClangdAstPath clangdAstPath = getAstPath(*clangdAst, Range(cursor));
                for (auto it = clangdAstPath.rbegin(); it != clangdAstPath.rend(); ++it) {
                    if (it->detailIs("operator->") && it->arcanaContains("CXXMethod"))
                        return cursor;
                }
                break;
            }
            default:
                return cursor;
            }
            if (posForToken(memberAccess->access_token) != cursor.position())
                return cursor;
            return leftMovedCursor();
        }

        // f(arg1|, arg2)
        if (const CallAST *const callAst = (*it)->asCall()) {
            const int tok = builtinAstPath.last()->lastToken();
            if (posForToken(tok) != cursor.position())
                return cursor;
            if (tok == callAst->rparen_token)
                return leftMovedCursor();
            if (tu->tokenKind(tok) != T_COMMA)
                return cursor;

            // Guard against edge case of overloaded comma operator.
            for (auto list = callAst->expression_list; list; list = list->next) {
                if (list->value->lastToken() == tok)
                    return leftMovedCursor();
            }
            return cursor;
        }

        // ~My|Class
        if (const DestructorNameAST * const destrAst = (*it)->asDestructorName()) {
            QTextCursor c = cursor;
            c.setPosition(posForToken(destrAst->tilde_token));
            return c;
        }

        // QList<QString|>
        if (const TemplateIdAST * const templAst = (*it)->asTemplateId()) {
            if (posForToken(templAst->greater_token) == cursor.position())
                return leftMovedCursor();
            return cursor;
        }
    }
    return cursor;
}

void ClangdClient::Private::setHelpItemForTooltip(const MessageId &token,
                                                  const Utils::FilePath &filePath,
                                                  const QString &fqn,
                                                  HelpItem::Category category,
                                                  const QString &type)
{
    QStringList helpIds;
    QString mark;
    if (!fqn.isEmpty()) {
        helpIds << fqn;
        int sepSearchStart = 0;
        while (true) {
            sepSearchStart = fqn.indexOf("::", sepSearchStart);
            if (sepSearchStart == -1)
                break;
            sepSearchStart += 2;
            helpIds << fqn.mid(sepSearchStart);
        }
        mark = helpIds.last();
        if (category == HelpItem::Function)
            mark += type.mid(type.indexOf('('));
    }
    if (category == HelpItem::Enum && !type.isEmpty())
        mark = type;

    const HelpItem helpItem(helpIds, filePath, mark, category);
    q->hoverHandler()->setHelpItem(token, helpItem);
    if (isTesting)
        emit q->helpItemGathered(helpItem, q->hoverHandler()->toolTip());
}

// Unfortunately, clangd ignores almost everything except symbols when sending
// semantic token info, so we need to consult the AST for additional information.
// In particular, we inspect the following constructs:
//    - Raw string literals, because our built-in lexer does not parse them properly.
//      While we're at it, we also handle other types of literals.
//    - Ternary expressions (for the matching of "?" and ":").
//    - Template declarations and instantiations (for the matching of "<" and ">").
//    - Function declarations, to find out whether a declaration is also a definition.
//    - Function arguments, to find out whether they correspond to output parameters.
//    - We consider most other tokens to be simple enough to be handled by the built-in code model.
//      Sometimes we have no choice, as for #include directives, which appear neither
//      in the semantic tokens nor in the AST.
void ClangdClient::Private::handleSemanticTokens(TextDocument *doc,
                                                 const QList<ExpandedSemanticToken> &tokens,
                                                 int version, bool force)
{
    SubtaskTimer t(highlightingTimer);
    qCInfo(clangdLogHighlight) << "handling LSP tokens" << doc->filePath()
                               << version << tokens.size();
    if (version != q->documentVersion(doc->filePath())) {
        qCInfo(clangdLogHighlight) << "LSP tokens outdated; aborting highlighting procedure"
                                    << version << q->documentVersion(doc->filePath());
        return;
    }
    force = force || isTesting;
    auto data = highlightingData.find(doc);
    if (data != highlightingData.end()) {
        if (!force && data->previousTokens.first == tokens
                && data->previousTokens.second == version) {
            qCInfo(clangdLogHighlight) << "tokens and version same as last time; nothing to do";
            return;
        }
        data->previousTokens.first = tokens;
        data->previousTokens.second = version;
    } else {
        data = highlightingData.insert(doc, {{tokens, version}, {}});
    }
    for (const ExpandedSemanticToken &t : tokens)
        qCDebug(clangdLogHighlight()) << '\t' << t.line << t.column << t.length << t.type
                                      << t.modifiers;

    FinalizingSubtaskTimer ft(highlightingTimer);
    if (!q->documentOpen(doc))
        return;
    if (version != q->documentVersion(doc->filePath())) {
        qCInfo(clangdLogHighlight) << "AST not up to date; aborting highlighting procedure"
                                   << version << q->documentVersion(doc->filePath());
        return;
    }

    const auto runner = [tokens, filePath = doc->filePath(),
                         text = doc->plainText(),
                         rev = doc->document()->revision(), this] {
        try {
            return Utils::asyncRun(doSemanticHighlighting, filePath, tokens, text,
                                   rev, highlightingTimer);
        } catch (const std::exception &e) {
            qWarning() << "caught" << e.what() << "in main highlighting thread";
            return QFuture<HighlightingResult>();
        }
    };

    if (isTesting) {
        const auto watcher = new QFutureWatcher<HighlightingResult>(q);
        connect(watcher, &QFutureWatcher<HighlightingResult>::finished,
                q, [this, watcher, fp = doc->filePath()] {
            emit q->highlightingResultsReady(watcher->future().results(), fp);
            watcher->deleteLater();
        });
        watcher->setFuture(runner());
        return;
    }

    if (!data->highlighter)
        data->highlighter = new CppEditor::SemanticHighlighter(doc);
    else
        data->highlighter->updateFormatMapFromFontSettings();
    data->highlighter->setHighlightingRunner(runner);
    data->highlighter->run();
}

std::optional<QList<CodeAction> > ClangdDiagnostic::codeActions() const
{
    auto actions = optionalArray<LanguageServerProtocol::CodeAction>(Key{"codeActions"});
    if (!actions)
        return actions;
    static const QStringList badCodeActions{
        "remove constant to silence this warning", // QTCREATORBUG-18593
    };
    for (auto it = actions->begin(); it != actions->end();) {
        if (badCodeActions.contains(it->title()))
            it = actions->erase(it);
        else
            ++it;
    }
    return actions;
}

QString ClangdDiagnostic::category() const
{
    return typedValue<QString>(Key{"category"});
}

MessageId ClangdClient::Private::getAndHandleAst(const TextDocOrFile &doc,
                                                 const AstHandler &astHandler,
                                                 AstCallbackMode callbackMode, const Range &range)
{
    const auto textDocPtr = std::get_if<const TextDocument *>(&doc);
    const TextDocument * const textDoc = textDocPtr ? *textDocPtr : nullptr;
    const Utils::FilePath filePath = textDoc ? textDoc->filePath()
                                             : std::get<Utils::FilePath>(doc);

    // If the entire AST is requested and the document's AST is in the cache and it is up to date,
    // call the handler.
    const bool fullAstRequested = !range.isValid();
    if (fullAstRequested) {
        if (const auto ast = textDoc ? astCache.get(textDoc) : externalAstCache.get(filePath)) {
            qCDebug(clangdLog) << "using AST from cache";
            switch (callbackMode) {
            case AstCallbackMode::SyncIfPossible:
                astHandler(*ast, {});
                break;
            case AstCallbackMode::AlwaysAsync:
                QMetaObject::invokeMethod(q, [ast, astHandler] { astHandler(*ast, {}); },
                                      Qt::QueuedConnection);
                break;
            }
            return {};
        }
    }

    // Otherwise retrieve the AST from clangd.
    const auto wrapperHandler = [this, filePath, guardedTextDoc = QPointer(textDoc), astHandler,
            fullAstRequested, docRev = textDoc ? getRevision(textDoc) : -1,
            fileRev = getRevision(filePath)](const ClangdAstNode &ast, const MessageId &reqId) {
        qCDebug(clangdLog) << "retrieved AST from clangd";
        if (fullAstRequested) {
            if (guardedTextDoc) {
                if (docRev == getRevision(guardedTextDoc))
                    astCache.insert(guardedTextDoc, ast);
            } else if (fileRev == getRevision(filePath) && !q->documentForFilePath(filePath)) {
                externalAstCache.insert(filePath, ast);
            }
        }
        astHandler(ast, reqId);
    };
    qCDebug(clangdLog) << "requesting AST for" << filePath;
    return requestAst(q, filePath, range, wrapperHandler);
}

} // namespace Internal
} // namespace ClangCodeModel
