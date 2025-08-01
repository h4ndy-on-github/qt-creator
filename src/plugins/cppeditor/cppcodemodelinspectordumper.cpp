// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppcodemodelinspectordumper.h"

#include "cppmodelmanager.h"
#include "cppprojectfile.h"
#include "cpptoolsreuse.h"
#include "cppworkingcopy.h"

#include <coreplugin/icore.h>

#include <projectexplorer/projectmacro.h>
#include <projectexplorer/project.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/temporarydirectory.h>

#include <cplusplus/CppDocument.h>
#include <cplusplus/Token.h>

#include <QDir>
#include <QList>
#include <QString>

namespace CppEditor {
namespace CppCodeModelInspector {

QString Utils::toString(bool value)
{
    return value ? QLatin1String("Yes") : QLatin1String("No");
}

QString Utils::toString(int value)
{
    return QString::number(value);
}

QString Utils::toString(unsigned value)
{
    return QString::number(value);
}

QString Utils::toString(const QDateTime &dateTime)
{
    return dateTime.toString(QLatin1String("hh:mm:ss dd.MM.yy"));
}

QString Utils::toString(CPlusPlus::Document::CheckMode checkMode)
{
#define CASE_CHECKMODE(x) case CPlusPlus::Document::x: return QLatin1String(#x)
    switch (checkMode) {
    CASE_CHECKMODE(Unchecked);
    CASE_CHECKMODE(FullCheck);
    CASE_CHECKMODE(FastCheck);
    // no default to get a compiler warning if anything is added
    }
#undef CASE_CHECKMODE
    return QString();
}

QString Utils::toString(CPlusPlus::Document::DiagnosticMessage::Level level)
{
#define CASE_LEVEL(x) case CPlusPlus::Document::DiagnosticMessage::x: return QLatin1String(#x)
    switch (level) {
    CASE_LEVEL(Warning);
    CASE_LEVEL(Error);
    CASE_LEVEL(Fatal);
    // no default to get a compiler warning if anything is added
    }
#undef CASE_LEVEL
    return QString();
}

QString Utils::toString(ProjectExplorer::HeaderPathType type)
{
#define CASE_LANGUAGEVERSION(x) case ProjectExplorer::HeaderPathType::x: return QLatin1String(#x"Path")
    switch (type) {
    CASE_LANGUAGEVERSION(User);
    CASE_LANGUAGEVERSION(System);
    CASE_LANGUAGEVERSION(Framework);
    CASE_LANGUAGEVERSION(BuiltIn);
    // no default to get a compiler warning if anything is added
    }
#undef CASE_LANGUAGEVERSION
    return QString();
}

QString Utils::toString(::Utils::LanguageVersion languageVersion)
{
#define CASE_LANGUAGEVERSION(x) case ::Utils::LanguageVersion::x: return QLatin1String(#x)
    switch (languageVersion) {
        CASE_LANGUAGEVERSION(None);
        CASE_LANGUAGEVERSION(C89);
        CASE_LANGUAGEVERSION(C99);
        CASE_LANGUAGEVERSION(C11);
        CASE_LANGUAGEVERSION(C18);
        CASE_LANGUAGEVERSION(CXX98);
        CASE_LANGUAGEVERSION(CXX03);
        CASE_LANGUAGEVERSION(CXX11);
        CASE_LANGUAGEVERSION(CXX14);
        CASE_LANGUAGEVERSION(CXX17);
        CASE_LANGUAGEVERSION(CXX20);
        CASE_LANGUAGEVERSION(CXX2b);
        // no default to get a compiler warning if anything is added
    }
#undef CASE_LANGUAGEVERSION
    return QString();
}

QString Utils::toString(::Utils::LanguageExtensions languageExtension)
{
    QString result;

#define CASE_LANGUAGE_EXTENSION(ext) if (languageExtension & ::Utils::LanguageExtension::ext) \
    result += QLatin1String(#ext ", ");

    CASE_LANGUAGE_EXTENSION(None);
    CASE_LANGUAGE_EXTENSION(Gnu);
    CASE_LANGUAGE_EXTENSION(Microsoft);
    CASE_LANGUAGE_EXTENSION(Borland);
    CASE_LANGUAGE_EXTENSION(OpenMP);
    CASE_LANGUAGE_EXTENSION(ObjectiveC);
#undef CASE_LANGUAGE_EXTENSION
    if (result.endsWith(QLatin1String(", ")))
        result.chop(2);
    return result;
}

QString Utils::toString(::Utils::QtMajorVersion qtVersion)
{
#define CASE_QTVERSION(x) \
    case ::Utils::QtMajorVersion::x: \
        return QLatin1String(#x)
    switch (qtVersion) {
        CASE_QTVERSION(Unknown);
        CASE_QTVERSION(None);
        CASE_QTVERSION(Qt4);
        CASE_QTVERSION(Qt5);
        CASE_QTVERSION(Qt6);
        // no default to get a compiler warning if anything is added
    }
#undef CASE_QTVERSION
    return QString();
}

QString Utils::toString(ProjectExplorer::BuildTargetType buildTargetType)
{
#define CASE_BUILDTARGETTYPE(x) \
    case ProjectExplorer::BuildTargetType::x: \
        return QLatin1String(#x)
    switch (buildTargetType) {
    CASE_BUILDTARGETTYPE(Unknown);
    CASE_BUILDTARGETTYPE(Executable);
    CASE_BUILDTARGETTYPE(Library);
    }
#undef CASE_BUILDTARGETTYPE
    return QString();
}

QString Utils::toString(ProjectFile::Kind kind)
{
    return QString::fromLatin1(projectFileKindToText(kind));
}

QString Utils::toString(CPlusPlus::Kind kind)
{
    using namespace CPlusPlus;
#define TOKEN(x) case x: return QLatin1String(#x)
#define TOKEN_AND_ALIASES(x,y) case x: return QLatin1String(#x "/" #y)
    switch (kind) {
    TOKEN(T_EOF_SYMBOL);
    TOKEN(T_ERROR);
    TOKEN(T_CPP_COMMENT);
    TOKEN(T_CPP_DOXY_COMMENT);
    TOKEN(T_COMMENT);
    TOKEN(T_DOXY_COMMENT);
    TOKEN(T_IDENTIFIER);
    TOKEN(T_NUMERIC_LITERAL);
    TOKEN(T_CHAR_LITERAL);
    TOKEN(T_WIDE_CHAR_LITERAL);
    TOKEN(T_UTF16_CHAR_LITERAL);
    TOKEN(T_UTF32_CHAR_LITERAL);
    TOKEN(T_STRING_LITERAL);
    TOKEN(T_WIDE_STRING_LITERAL);
    TOKEN(T_UTF8_STRING_LITERAL);
    TOKEN(T_UTF16_STRING_LITERAL);
    TOKEN(T_UTF32_STRING_LITERAL);
    TOKEN(T_RAW_STRING_LITERAL);
    TOKEN(T_RAW_WIDE_STRING_LITERAL);
    TOKEN(T_RAW_UTF8_STRING_LITERAL);
    TOKEN(T_RAW_UTF16_STRING_LITERAL);
    TOKEN(T_RAW_UTF32_STRING_LITERAL);
    TOKEN(T_AT_STRING_LITERAL);
    TOKEN(T_ANGLE_STRING_LITERAL);
    TOKEN_AND_ALIASES(T_AMPER, T_BITAND);
    TOKEN_AND_ALIASES(T_AMPER_AMPER, T_AND);
    TOKEN_AND_ALIASES(T_AMPER_EQUAL, T_AND_EQ);
    TOKEN(T_ARROW);
    TOKEN(T_ARROW_STAR);
    TOKEN_AND_ALIASES(T_CARET, T_XOR);
    TOKEN_AND_ALIASES(T_CARET_EQUAL, T_XOR_EQ);
    TOKEN(T_COLON);
    TOKEN(T_COLON_COLON);
    TOKEN(T_COMMA);
    TOKEN(T_SLASH);
    TOKEN(T_SLASH_EQUAL);
    TOKEN(T_DOT);
    TOKEN(T_DOT_DOT_DOT);
    TOKEN(T_DOT_STAR);
    TOKEN(T_EQUAL);
    TOKEN(T_EQUAL_EQUAL);
    TOKEN_AND_ALIASES(T_EXCLAIM, T_NOT);
    TOKEN_AND_ALIASES(T_EXCLAIM_EQUAL, T_NOT_EQ);
    TOKEN(T_GREATER);
    TOKEN(T_GREATER_EQUAL);
    TOKEN(T_GREATER_GREATER);
    TOKEN(T_GREATER_GREATER_EQUAL);
    TOKEN(T_LBRACE);
    TOKEN(T_LBRACKET);
    TOKEN(T_LESS);
    TOKEN(T_LESS_EQUAL);
    TOKEN(T_LESS_EQUAL_GREATER);
    TOKEN(T_LESS_LESS);
    TOKEN(T_LESS_LESS_EQUAL);
    TOKEN(T_LPAREN);
    TOKEN(T_MINUS);
    TOKEN(T_MINUS_EQUAL);
    TOKEN(T_MINUS_MINUS);
    TOKEN(T_PERCENT);
    TOKEN(T_PERCENT_EQUAL);
    TOKEN_AND_ALIASES(T_PIPE, T_BITOR);
    TOKEN_AND_ALIASES(T_PIPE_EQUAL, T_OR_EQ);
    TOKEN_AND_ALIASES(T_PIPE_PIPE, T_OR);
    TOKEN(T_PLUS);
    TOKEN(T_PLUS_EQUAL);
    TOKEN(T_PLUS_PLUS);
    TOKEN(T_POUND);
    TOKEN(T_POUND_POUND);
    TOKEN(T_QUESTION);
    TOKEN(T_RBRACE);
    TOKEN(T_RBRACKET);
    TOKEN(T_RPAREN);
    TOKEN(T_SEMICOLON);
    TOKEN(T_STAR);
    TOKEN(T_STAR_EQUAL);
    TOKEN_AND_ALIASES(T_TILDE, T_COMPL);
    TOKEN(T_TILDE_EQUAL);
    TOKEN(T_ALIGNAS);
    TOKEN(T_ALIGNOF);
    TOKEN_AND_ALIASES(T_ASM, T___ASM/T___ASM__);
    TOKEN(T_AUTO);
    TOKEN(T_BOOL);
    TOKEN(T_BREAK);
    TOKEN(T_CASE);
    TOKEN(T_CATCH);
    TOKEN(T_CHAR);
    TOKEN(T_CHAR8_T);
    TOKEN(T_CHAR16_T);
    TOKEN(T_CHAR32_T);
    TOKEN(T_CLASS);
    TOKEN(T_CO_AWAIT);
    TOKEN(T_CO_RETURN);
    TOKEN(T_CO_YIELD);
    TOKEN(T_CONCEPT);
    TOKEN_AND_ALIASES(T_CONST, T___CONST/T___CONST__);
    TOKEN(T_CONST_CAST);
    TOKEN(T_CONSTEXPR);
    TOKEN(T_CONSTEVAL);
    TOKEN(T_CONSTINIT);
    TOKEN(T_CONTINUE);
    TOKEN_AND_ALIASES(T_DECLTYPE, T___DECLTYPE);
    TOKEN(T_DEFAULT);
    TOKEN(T_DELETE);
    TOKEN(T_DO);
    TOKEN(T_DOUBLE);
    TOKEN(T_DYNAMIC_CAST);
    TOKEN(T_ELSE);
    TOKEN(T_ENUM);
    TOKEN(T_EXPLICIT);
    TOKEN(T_EXPORT);
    TOKEN(T_EXTERN);
    TOKEN(T_FALSE);
    TOKEN(T_FLOAT);
    TOKEN(T_FOR);
    TOKEN(T_FRIEND);
    TOKEN(T_GOTO);
    TOKEN(T_IF);
    TOKEN_AND_ALIASES(T_INLINE, T___INLINE/T___INLINE__);
    TOKEN(T_INT);
    TOKEN(T_LONG);
    TOKEN(T_MUTABLE);
    TOKEN(T_NAMESPACE);
    TOKEN(T_NEW);
    TOKEN(T_NOEXCEPT);
    TOKEN(T_NULLPTR);
    TOKEN(T_OPERATOR);
    TOKEN(T_PRIVATE);
    TOKEN(T_PROTECTED);
    TOKEN(T_PUBLIC);
    TOKEN(T_REGISTER);
    TOKEN(T_REINTERPRET_CAST);
    TOKEN(T_REQUIRES);
    TOKEN(T_RETURN);
    TOKEN(T_SHORT);
    TOKEN(T_SIGNED);
    TOKEN(T_SIZEOF);
    TOKEN(T_STATIC);
    TOKEN(T_STATIC_ASSERT);
    TOKEN(T_STATIC_CAST);
    TOKEN(T_STRUCT);
    TOKEN(T_SWITCH);
    TOKEN(T_TEMPLATE);
    TOKEN(T_THIS);
    TOKEN(T_THREAD_LOCAL);
    TOKEN(T_THROW);
    TOKEN(T_TRUE);
    TOKEN(T_TRY);
    TOKEN(T_TYPEDEF);
    TOKEN(T_TYPEID);
    TOKEN(T_TYPENAME);
    TOKEN(T_UNION);
    TOKEN(T_UNSIGNED);
    TOKEN(T_USING);
    TOKEN(T_VIRTUAL);
    TOKEN(T_VOID);
    TOKEN_AND_ALIASES(T_VOLATILE, T___VOLATILE/T___VOLATILE__);
    TOKEN(T_WCHAR_T);
    TOKEN(T_WHILE);
    TOKEN_AND_ALIASES(T___ATTRIBUTE__, T___ATTRIBUTE);
    TOKEN(T___THREAD);
    TOKEN_AND_ALIASES(T___TYPEOF__, T_TYPEOF/T___TYPEOF);
    TOKEN_AND_ALIASES(T___DECLSPEC, T__DECLSPEC);
    TOKEN(T_AT_CATCH);
    TOKEN(T_AT_CLASS);
    TOKEN(T_AT_COMPATIBILITY_ALIAS);
    TOKEN(T_AT_DEFS);
    TOKEN(T_AT_DYNAMIC);
    TOKEN(T_AT_ENCODE);
    TOKEN(T_AT_END);
    TOKEN(T_AT_FINALLY);
    TOKEN(T_AT_IMPLEMENTATION);
    TOKEN(T_AT_INTERFACE);
    TOKEN(T_AT_NOT_KEYWORD);
    TOKEN(T_AT_OPTIONAL);
    TOKEN(T_AT_PACKAGE);
    TOKEN(T_AT_PRIVATE);
    TOKEN(T_AT_PROPERTY);
    TOKEN(T_AT_PROTECTED);
    TOKEN(T_AT_PROTOCOL);
    TOKEN(T_AT_PUBLIC);
    TOKEN(T_AT_REQUIRED);
    TOKEN(T_AT_SELECTOR);
    TOKEN(T_AT_SYNCHRONIZED);
    TOKEN(T_AT_SYNTHESIZE);
    TOKEN(T_AT_THROW);
    TOKEN(T_AT_TRY);
    TOKEN(T_EMIT);
    TOKEN(T_SIGNAL);
    TOKEN(T_SLOT);
    TOKEN(T_Q_SIGNAL);
    TOKEN(T_Q_SLOT);
    TOKEN(T_Q_SIGNALS);
    TOKEN(T_Q_SLOTS);
    TOKEN(T_Q_FOREACH);
    TOKEN(T_Q_D);
    TOKEN(T_Q_Q);
    TOKEN(T_Q_INVOKABLE);
    TOKEN(T_Q_PROPERTY);
    TOKEN(T_Q_PRIVATE_PROPERTY);
    TOKEN(T_Q_INTERFACES);
    TOKEN(T_Q_EMIT);
    TOKEN(T_Q_ENUMS);
    TOKEN(T_Q_FLAGS);
    TOKEN(T_Q_PRIVATE_SLOT);
    TOKEN(T_Q_DECLARE_INTERFACE);
    TOKEN(T_Q_OBJECT);
    TOKEN(T_Q_GADGET);
    // no default to get a compiler warning if anything is added
    }
#undef TOKEN
#undef TOKEN_AND_ALIASES
    return QString();
}

QString Utils::toString(const ProjectExplorer::Abi &abi)
{
    switch (abi.wordWidth()) {
    case 32:
        return QString("32");
    case 64:
        return QString("64");
    }
    return QString("??");
}

QString Utils::partsForFile(const ::Utils::FilePath &filePath)
{
    const QList<ProjectPart::ConstPtr> parts
        = CppModelManager::projectPart(filePath);
    QString result;
    for (const ProjectPart::ConstPtr &part : parts)
        result += part->displayName + QLatin1Char(',');
    if (result.endsWith(QLatin1Char(',')))
        result.chop(1);
    return result;
}

QString Utils::unresolvedFileNameWithDelimiters(const CPlusPlus::Document::Include &include)
{
    const QString unresolvedFileName = include.unresolvedFileName();
    if (include.type() == CPlusPlus::Client::IncludeLocal)
        return QLatin1Char('"') + unresolvedFileName + QLatin1Char('"');
    return QLatin1Char('<') + unresolvedFileName + QLatin1Char('>');
}

QString Utils::pathListToString(const ProjectExplorer::HeaderPaths &pathList)
{
    QStringList result;
    for (const ProjectExplorer::HeaderPath &path : pathList)
        result << QString(QLatin1String("%1 (%2 path)")).arg(
                      path.path.nativePath(), toString(path.type));

    return result.join(QLatin1Char('\n'));
}

QList<CPlusPlus::Document::Ptr> Utils::snapshotToList(const CPlusPlus::Snapshot &snapshot)
{
    QList<CPlusPlus::Document::Ptr> documents;
    CPlusPlus::Snapshot::const_iterator it = snapshot.begin(), end = snapshot.end();
    for (; it != end; ++it)
        documents.append(it.value());
    return documents;
}

Dumper::Dumper(const CPlusPlus::Snapshot &globalSnapshot, const QString &logFileId)
    : m_globalSnapshot(globalSnapshot), m_out(stderr)
{
    QString ideRevision;
#ifdef IDE_REVISION
     ideRevision = QString::fromLatin1(Core::Constants::IDE_REVISION_STR).left(10);
#endif
    QString ideRevision_ = ideRevision;
    if (!ideRevision_.isEmpty())
        ideRevision_.prepend(QLatin1Char('_'));
    QString logFileId_ = logFileId;
    if (!logFileId_.isEmpty())
        logFileId_.prepend(QLatin1Char('_'));
    const QString logFileName = ::Utils::TemporaryDirectory::masterDirectoryPath()
            + "/qtc-codemodelinspection"
            + ideRevision_
            + QDateTime::currentDateTime().toString("_yyMMdd_hhmmss")
            + logFileId_
            + QLatin1String(".txt");

    m_logFile.setFileName(logFileName);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_out << "Code model inspection log file is \"" << QDir::toNativeSeparators(logFileName)
              << "\".\n";
        m_out.setDevice(&m_logFile);
    }
    m_out << "*** START Code Model Inspection Report for ";
    m_out << Core::ICore::versionString() << " from revision " << ideRevision << "\n";
    m_out << "Note: This file contains vim fold markers (\"{{{n\"). "
             "Make use of them via \":set foldmethod=marker\".\n";
}

Dumper::~Dumper()
{
    m_out << "*** END Code Model Inspection Report\n";
}

static void printIncludeType(QTextStream &out, ProjectExplorer::HeaderPathType type)
{
    using ProjectExplorer::HeaderPathType;
    switch (type) {
        case HeaderPathType::User: out << "(user include path)"; break;
        case HeaderPathType::System: out << "(system include path)"; break;
        case HeaderPathType::Framework: out << "(framework path)"; break;
        case HeaderPathType::BuiltIn: out << "(built-in include path)"; break;
    }
}

void Dumper::dumpProjectInfos(const QList<ProjectInfo::ConstPtr> &projectInfos)
{
    const QByteArray i1 = indent(1);
    const QByteArray i2 = indent(2);
    const QByteArray i3 = indent(3);
    const QByteArray i4 = indent(4);

    m_out << "Projects loaded: " << projectInfos.size() << "{{{1\n";
    for (const ProjectInfo::ConstPtr &info : projectInfos) {
        m_out << i1 << "Project " << info->projectName()
              << " (" << info->projectFilePath().toUserOutput() << "){{{2\n";

        const QList<ProjectPart::ConstPtr> projectParts = info->projectParts();
        for (const ProjectPart::ConstPtr &part : projectParts) {
            QString projectName = QLatin1String("<None>");
            QString projectFilePath = "<None>";
            if (part->hasProject()) {
                projectFilePath = part->topLevelProject.toUserOutput();
                if (const ProjectExplorer::Project * const project = part->project())
                    projectName = project->displayName();
            }
            if (!part->projectConfigFile.isEmpty())
                m_out << i3 << "Project Config File: " << part->projectConfigFile << "\n";
            m_out << i2 << "Project Part \"" << part->id() << "\"{{{3\n";
            m_out << i3 << "Project Part Name      : " << part->displayName << "\n";
            m_out << i3 << "Project Name           : " << projectName << "\n";
            m_out << i3 << "Project File           : " << projectFilePath << "\n";
            m_out << i3 << "ToolChain Type         : " << part->toolchainType.toString() << "\n";
            m_out << i3 << "ToolChain Target Triple: " << part->toolchainTargetTriple << "\n";
            m_out << i3 << "ToolChain Word Width   : " << part->toolchainAbi.wordWidth() << "\n";
            m_out << i3 << "ToolChain Install Dir  : " << part->toolchainInstallDir << "\n";
            m_out << i3 << "Compiler Flags         : " << part->compilerFlags.join(", ") << "\n";
            m_out << i3 << "Selected For Building  : " << part->selectedForBuilding << "\n";
            m_out << i3 << "Build System Target    : " << part->buildSystemTarget << "\n";
            m_out << i3 << "Build Target Type      : " << Utils::toString(part->buildTargetType) << "\n";
            m_out << i3 << "Language Version       : " << Utils::toString(part->languageVersion)<<"\n";
            m_out << i3 << "Language Extensions    : " << Utils::toString(part->languageExtensions)
                  << "\n";
            m_out << i3 << "Qt Version             : " << Utils::toString(part->qtVersion) << "\n";

            if (!part->files.isEmpty()) {
                m_out << i3 << "Files:{{{4\n";
                for (const ProjectFile &projectFile : std::as_const(part->files)) {
                    m_out << i4 << Utils::toString(projectFile.kind) << ": " << projectFile.path;
                    if (!projectFile.active)
                        m_out << " (inactive)";
                    m_out << "\n";
                }
            }

            if (!part->toolchainMacros.isEmpty()) {
                m_out << i3 << "Toolchain Defines:{{{4\n";
                const QList<QByteArray> defineLines =
                        ProjectExplorer::Macro::toByteArray(part->toolchainMacros).split('\n');
                for (const QByteArray &defineLine : defineLines)
                    m_out << i4 << defineLine << "\n";
            }
            if (!part->projectMacros.isEmpty()) {
                m_out << i3 << "Project Defines:{{{4\n";
                const QList<QByteArray> defineLines =
                        ProjectExplorer::Macro::toByteArray(part->projectMacros).split('\n');
                for (const QByteArray &defineLine : defineLines)
                    m_out << i4 << defineLine << "\n";
            }

            if (!part->headerPaths.isEmpty()) {
                m_out << i3 << "Header Paths:{{{4\n";
                for (const ProjectExplorer::HeaderPath &headerPath : std::as_const(part->headerPaths)) {
                    m_out << i4 << headerPath.path;
                    printIncludeType(m_out, headerPath.type);
                    m_out << "\n";
                }
            }

            if (!part->precompiledHeaders.isEmpty()) {
                m_out << i3 << "Precompiled Headers:{{{4\n";
                for (const ::Utils::FilePath &precompiledHeader : std::as_const(part->precompiledHeaders))
                    m_out << i4 << precompiledHeader << "\n";
            }
        } // for part
    } // for project Info
}

void Dumper::dumpSnapshot(const CPlusPlus::Snapshot &snapshot, const QString &title,
                          bool isGlobalSnapshot)
{
    m_out << "Snapshot \"" << title << "\"{{{1\n";

    const QByteArray i1 = indent(1);
    const QList<CPlusPlus::Document::Ptr> documents = Utils::snapshotToList(snapshot);

    if (isGlobalSnapshot) {
        if (!documents.isEmpty()) {
            m_out << i1 << "Globally-Shared documents{{{2\n";
            dumpDocuments(documents, false);
        }
    } else {
        // Divide into shared and not shared
        QList<CPlusPlus::Document::Ptr> globallyShared;
        QList<CPlusPlus::Document::Ptr> notGloballyShared;
        for (const CPlusPlus::Document::Ptr &document : documents) {
            CPlusPlus::Document::Ptr globalDocument = m_globalSnapshot.document(document->filePath());
            if (globalDocument && globalDocument->fingerprint() == document->fingerprint())
                globallyShared.append(document);
            else
                notGloballyShared.append(document);
        }

        if (!notGloballyShared.isEmpty()) {
            m_out << i1 << "Not-Globally-Shared documents:{{{2\n";
            dumpDocuments(notGloballyShared);
        }
        if (!globallyShared.isEmpty()) {
            m_out << i1 << "Globally-Shared documents{{{2\n";
            dumpDocuments(globallyShared, true);
        }
    }
}

void Dumper::dumpWorkingCopy(const WorkingCopy &workingCopy)
{
    m_out << "Working Copy contains " << workingCopy.size() << " entries{{{1\n";

    const QByteArray i1 = indent(1);
    const WorkingCopy::Table &elements = workingCopy.elements();
    for (auto it = elements.cbegin(), end = elements.cend(); it != end; ++it) {
        const ::Utils::FilePath &filePath = it.key();
        unsigned sourcRevision = it.value().second;
        m_out << i1 << "rev=" << sourcRevision << ", " << filePath << "\n";
    }
}

void Dumper::dumpMergedEntities(const ProjectExplorer::HeaderPaths &mergedHeaderPaths,
                                const QByteArray &mergedMacros)
{
    m_out << "Merged Entities{{{1\n";
    const QByteArray i2 = indent(2);
    const QByteArray i3 = indent(3);

    m_out << i2 << "Merged Header Paths{{{2\n";
    for (const ProjectExplorer::HeaderPath &hp : mergedHeaderPaths) {
        m_out << i3 << hp.path;
        printIncludeType(m_out, hp.type);
        m_out << "\n";
    }
    m_out << i2 << "Merged Defines{{{2\n";
    m_out << mergedMacros;
}

void Dumper::dumpStringList(const QStringList &list, const QByteArray &indent)
{
    for (const QString &item : list)
        m_out << indent << item << "\n";
}

void Dumper::dumpDocuments(const QList<CPlusPlus::Document::Ptr> &documents, bool skipDetails)
{
    const QByteArray i2 = indent(2);
    const QByteArray i3 = indent(3);
    const QByteArray i4 = indent(4);
    for (const CPlusPlus::Document::Ptr &document : documents) {
        if (skipDetails) {
            m_out << i2 << "\"" << document->filePath() << "\"\n";
            continue;
        }

        m_out << i2 << "Document \"" << document->filePath() << "\"{{{3\n";
        m_out << i3 << "Last Modified  : " << Utils::toString(document->lastModified()) << "\n";
        m_out << i3 << "Revision       : " << Utils::toString(document->revision()) << "\n";
        m_out << i3 << "Editor Revision: " << Utils::toString(document->editorRevision()) << "\n";
        m_out << i3 << "Check Mode     : " << Utils::toString(document->checkMode()) << "\n";
        m_out << i3 << "Tokenized      : " << Utils::toString(document->isTokenized()) << "\n";
        m_out << i3 << "Parsed         : " << Utils::toString(document->isParsed()) << "\n";
        m_out << i3 << "Project Parts  : " << Utils::partsForFile(document->filePath()) << "\n";

        const QList<CPlusPlus::Document::Include> includes = document->unresolvedIncludes()
                + document->resolvedIncludes();
        if (!includes.isEmpty()) {
            m_out << i3 << "Includes:{{{4\n";
            for (const CPlusPlus::Document::Include &include : includes) {
                m_out << i4 << "at line " << include.line() << ": "
                      << Utils::unresolvedFileNameWithDelimiters(include) << " ==> "
                      << include.resolvedFileName() << "\n";
            }
        }

        const QList<CPlusPlus::Document::DiagnosticMessage> diagnosticMessages
                = document->diagnosticMessages();
        if (!diagnosticMessages.isEmpty()) {
            m_out << i3 << "Diagnostic Messages:{{{4\n";
            for (const CPlusPlus::Document::DiagnosticMessage &msg : diagnosticMessages) {
                const auto level =
                    static_cast<CPlusPlus::Document::DiagnosticMessage::Level>(msg.level());
                m_out << i4 << "at " << msg.line() << ":" << msg.column() << ", " << Utils::toString(level)
                      << ": " << msg.text() << "\n";
            }
        }

        const QList<CPlusPlus::Macro> macroDefinitions = document->definedMacros();
        if (!macroDefinitions.isEmpty()) {
            m_out << i3 << "(Un)Defined Macros:{{{4\n";
            for (const CPlusPlus::Macro &macro : macroDefinitions)
                m_out << i4 << "at line " << macro.line() << ": " << macro.toString() << "\n";
        }

        const QList<CPlusPlus::Document::MacroUse> macroUses = document->macroUses();
        if (!macroUses.isEmpty()) {
            m_out << i3 << "Macro Uses:{{{4\n";
            for (const CPlusPlus::Document::MacroUse &use : macroUses) {
                const QString type = use.isFunctionLike()
                        ? QLatin1String("function-like") : QLatin1String("object-like");
                m_out << i4 << "at line " << use.beginLine() << ", "
                      << use.macro().nameToQString().size()
                      << ", begin=" << use.utf16charsBegin() << ", end=" << use.utf16charsEnd()
                      << ", " << type << ", args="
                      << use.arguments().size() << "\n";
            }
        }

        const QString source = QString::fromUtf8(document->utf8Source());
        if (!source.isEmpty()) {
            m_out << i4 << "Source:{{{4\n";
            m_out << source;
            m_out << "\n<<<EOF\n";
        }
    }
}

QByteArray Dumper::indent(int level)
{
    const QByteArray basicIndent("  ");
    QByteArray indent = basicIndent;
    while (level-- > 1)
        indent += basicIndent;
    return indent;
}

} // namespace CppCodeModelInspector
} // namespace CppEditor
