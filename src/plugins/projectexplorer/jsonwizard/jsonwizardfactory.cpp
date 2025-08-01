// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "jsonwizardfactory.h"

#include "jsonwizard.h"
#include "jsonwizardgeneratorfactory.h"
#include "jsonwizardpagefactory.h"
#include "../projectexplorerconstants.h"
#include "../projectexplorertr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/jsexpander.h>
#include <coreplugin/messagemanager.h>

#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/stringutils.h>
#include <utils/wizard.h>
#include <utils/wizardpage.h>

#include <QDebug>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QUuid>

using namespace Utils;
using namespace ExtensionSystem;

namespace ProjectExplorer {

const char WIZARD_PATH[] = "templates/wizards";

const char VERSION_KEY[] = "version";
const char ENABLED_EXPRESSION_KEY[] = "enabled";
const char SKIP_FOR_SUBPROJECTS_KEY[] = "skipForSubprojects";

const char KIND_KEY[] = "kind";
const char SUPPORTED_PROJECTS[] = "supportedProjectTypes";
const char ID_KEY[] = "id";
const char CATEGORY_KEY[] = "category";
const char CATEGORY_NAME_KEY[] = "trDisplayCategory";
const char DISPLAY_NAME_KEY[] = "trDisplayName";
const char ICON_KEY[] = "icon";
const char ICON_TEXT_KEY[] = "iconText";
const char FONT_ICON_NAME_KEY[] = "fontIconName";
const char IMAGE_KEY[] = "image";
const char ICON_KIND_KEY[] = "iconKind";
const char DESCRIPTION_KEY[] = "trDescription";
const char REQUIRED_FEATURES_KEY[] = "featuresRequired";
const char SUGGESTED_FEATURES_KEY[] = "featuresSuggested";
const char GENERATOR_KEY[] = "generators";
const char PAGES_KEY[] = "pages";
const char TYPE_ID_KEY[] = "typeId";
const char DATA_KEY[] = "data";
const char PAGE_SUB_TITLE_KEY[] = "trSubTitle";
const char PAGE_SHORT_TITLE_KEY[] = "trShortTitle";
const char PAGE_INDEX_KEY[] = "index";
const char OPTIONS_KEY[] = "options";
const char PLATFORM_INDEPENDENT_KEY[] = "platformIndependent";
const char DEFAULT_VALUES[] = "defaultValues";

static QList<JsonWizardPageFactory *> &pageFactories()
{
    static QList<JsonWizardPageFactory *> thePageFactories;
    return thePageFactories;
}

static QList<JsonWizardGeneratorFactory *> &generatorFactories()
{
    static QList<JsonWizardGeneratorFactory *> theGeneratorFactories;
    return theGeneratorFactories;
}

namespace Internal {

class JsonWizardFactoryJsExtension final : public QObject
{
    Q_OBJECT

public:
    JsonWizardFactoryJsExtension(Id platformId,
                                 const QSet<Id> &availableFeatures,
                                 const QSet<Id> &pluginFeatures)
        : m_platformId(platformId)
        , m_availableFeatures(availableFeatures)
        , m_pluginFeatures(pluginFeatures)
    {}

    Q_INVOKABLE QVariant value(const QString &name) const
    {
        if (name == "Platform")
            return m_platformId.toString();
        if (name == "Features")
            return Id::toStringList(m_availableFeatures);
        if (name == "Plugins")
            return Id::toStringList(m_pluginFeatures);
        return {};
    }
    Q_INVOKABLE bool isPluginRunning(const QString &id) const
    {
        return Internal::isAnyPluginRunning({id});
    }
    Q_INVOKABLE bool isAnyPluginRunning(const QStringList &ids) const
    {
        return Internal::isAnyPluginRunning(ids);
    }

private:
    Id m_platformId;
    QSet<Id> m_availableFeatures;
    QSet<Id> m_pluginFeatures;
};

} // namespace Internal
int JsonWizardFactory::m_verbose = 0;


// Return locale language attribute "de_UTF8" -> "de", empty string for "C"
static QString languageSetting()
{
    QString name = Core::ICore::userInterfaceLanguage();
    const int underScorePos = name.indexOf(QLatin1Char('_'));
    if (underScorePos != -1)
        name.truncate(underScorePos);
    if (name.compare(QLatin1String("C"), Qt::CaseInsensitive) == 0)
        name.clear();
    return name;
}

template<class T>
static QString supportedTypeIds(const QList<T *> &factories)
{
    QStringList tmp;
    for (const T *f : factories) {
        const QList<Id> ids = f->supportedIds();
        for (const Id i : ids)
            tmp.append(i.toString());
    }
    return tmp.join(QLatin1String("', '"));
}

static Result<JsonWizardFactory::Generator> parseGenerator(const QVariant &value)
{
    if (value.typeId() != QMetaType::QVariantMap)
        return ResultError(Tr::tr("Generator is not a object."));

    QVariantMap data = value.toMap();
    QString strVal = data.value(QLatin1String(TYPE_ID_KEY)).toString();
    if (strVal.isEmpty())
        return ResultError(Tr::tr("Generator has no typeId set."));

    Id typeId = Id(Constants::GENERATOR_ID_PREFIX).withSuffix(strVal);
    JsonWizardGeneratorFactory *factory
            = findOr(generatorFactories(), nullptr, [typeId](JsonWizardGeneratorFactory *f) { return f->canCreate(typeId); });
    if (!factory) {
        return ResultError(Tr::tr("TypeId \"%1\" of generator is unknown. Supported typeIds are: \"%2\".")
                .arg(strVal)
                .arg(supportedTypeIds(generatorFactories())
                    .replace(QLatin1String(Constants::GENERATOR_ID_PREFIX), QLatin1String())));
    }

    QVariant varVal = data.value(QLatin1String(DATA_KEY));
    if (const Result<> res = factory->validateData(typeId, varVal); !res)
        return ResultError(res.error());

    JsonWizardFactory::Generator gen;
    gen.typeId = typeId;
    gen.data = varVal;

    return gen;
}

JsonWizardPageFactory::JsonWizardPageFactory()
{
    pageFactories().append(this);
}

JsonWizardPageFactory::~JsonWizardPageFactory()
{
    pageFactories().removeOne(this);
}

JsonWizardGeneratorFactory::JsonWizardGeneratorFactory()
{
    generatorFactories().append(this);
}

JsonWizardGeneratorFactory::~JsonWizardGeneratorFactory()
{
    generatorFactories().removeOne(this);
}

//FIXME: createWizardFactories() has an almost identical loop. Make the loop return the results instead of
//internal processing and create a separate function for it. Then process the results in
//loadDefaultValues() and createWizardFactories()
QVariantMap JsonWizardFactory::loadDefaultValues(const QString &fileName)
{
    QString verboseLog;

    if (fileName.isEmpty()) {
        return {};
    }

    const Utils::FilePaths paths = searchPaths();
    for (const Utils::FilePath &path : paths) {
        if (path.isEmpty())
            continue;

        FilePath dir = FilePath::fromString(path.toUrlishString());
        if (!dir.exists()) {
            if (verbose())
                verboseLog.append(
                    Tr::tr("Path \"%1\" does not exist when checking JSON wizard search paths.")
                        .arg(path.toUserOutput())
                    + "\n");
            continue;
        }

        const QDir::Filters filters = QDir::Dirs|QDir::Readable|QDir::NoDotAndDotDot;
        FilePaths dirs = dir.dirEntries(filters);

        while (!dirs.isEmpty()) {
            const FilePath current = dirs.takeFirst();
            if (verbose())
                verboseLog.append(
                    Tr::tr("Checking \"%1\" for %2.")
                        .arg(QDir::toNativeSeparators(current.absolutePath().toUrlishString()))
                        .arg(fileName)
                    + "\n");
            if (current.pathAppended(fileName).exists()) {
                QFile configFile(current.pathAppended(fileName).toUrlishString());
                QTC_CHECK(configFile.open(QIODevice::ReadOnly));
                QJsonParseError error;
                const QByteArray fileData = configFile.readAll();
                const QJsonDocument json = QJsonDocument::fromJson(fileData, &error);
                configFile.close();

                if (error.error != QJsonParseError::NoError) {
                    int line = 1;
                    int column = 1;
                    for (int i = 0; i < error.offset; ++i) {
                        if (fileData.at(i) == '\n') {
                            ++line;
                            column = 1;
                        } else {
                            ++column;
                        }
                    }
                    verboseLog.append(
                        Tr::tr("* Failed to parse \"%1\":%2:%3: %4")
                            .arg(configFile.fileName())
                            .arg(line)
                            .arg(column)
                            .arg(error.errorString())
                        + "\n");
                    continue;
                }

                if (!json.isObject()) {
                    verboseLog.append(
                        Tr::tr("* Did not find a JSON object in \"%1\".").arg(configFile.fileName())
                        + "\n");
                    continue;
                }

                if (verbose())
                    verboseLog.append(Tr::tr("* Configuration found and parsed.") + "\n");

                return json.object().toVariantMap();
            }
            FilePaths subDirs = current.dirEntries(filters);
            if (!subDirs.isEmpty()) {
                // There is no QList::prepend(QList)...
                dirs.swap(subDirs);
                dirs.append(subDirs);
            } else if (verbose()) {
                verboseLog.append(Tr::tr("JsonWizard: \"%1\" not found.").arg(fileName) + "\n");
            }
        }
    }

    if (verbose()) { // Print to output pane for Windows.
        qWarning("%s", qPrintable(verboseLog));
        Core::MessageManager::writeDisrupting(verboseLog);
    }

    return {};
}

QVariant JsonWizardFactory::mergeDataValueMaps(const QVariant &valueMap, const QVariant &defaultValueMap)
{
    QVariantMap retVal;

    retVal.insert(defaultValueMap.toMap());
    retVal.insert(valueMap.toMap());
    return retVal;
}

QVariant JsonWizardFactory::getDataValue(const QLatin1String &key, const QVariantMap &valueSet,
                                         const QVariantMap &defaultValueSet, const QVariant &notExistValue)
{
    QVariant retVal = {};

    if ((valueSet.contains(key) && valueSet.value(key).typeId() == QMetaType::QVariantMap) ||
        (defaultValueSet.contains(key) && defaultValueSet.value(key).typeId() == QMetaType::QVariantMap)) {
        retVal = mergeDataValueMaps(valueSet.value(key), defaultValueSet.value(key));
    } else {
        QVariant defaultValue = defaultValueSet.value(key, notExistValue);
        retVal = valueSet.value(key, defaultValue);
    }

    return retVal;
}

std::pair<int, QStringList> JsonWizardFactory::screenSizeInfoFromPage(const QString &pageType) const
{
    /* Retrieving the ScreenFactor "trKey" values from pages[i]/data[j]/data["items"], where
     * pages[i] is the page of type `pageType` and data[j] is the data item with name ScreenFactor
    */

    const Utils::Id id = Utils::Id(Constants::PAGE_ID_PREFIX).withSuffix(pageType);

    const auto it = std::find_if(std::cbegin(m_pages), std::cend(m_pages), [&id](const Page &page) {
        return page.typeId == id;
    });

    if (it == std::cend(m_pages))
        return {};

    const QVariant data = it->data;
    if (data.typeId() != QMetaType::QVariantList)
        return {};

    const QVariant screenFactorField = Utils::findOrDefault(data.toList(),
                                                            [](const QVariant &field) {
                                                                const QVariantMap m = field.toMap();
                                                                return "ScreenFactor" == m["name"];
                                                            });

    if (screenFactorField.typeId() != QMetaType::QVariantMap)
        return {};

    const QVariant screenFactorData = screenFactorField.toMap()["data"];
    if (screenFactorData.typeId() != QMetaType::QVariantMap)
        return {};

    const QVariantMap screenFactorDataMap = screenFactorData.toMap();
    if (!screenFactorDataMap.contains("items"))
        return {};

    bool ok = false;
    const int index = screenFactorDataMap["index"].toInt(&ok);
    const QVariantList items = screenFactorDataMap["items"].toList();
    if (items.isEmpty())
        return {};

    QStringList values = Utils::transform(items, [](const QVariant &item) {
        const QVariantMap m = item.toMap();
        return m["trKey"].toString();
    });

    if (values.isEmpty())
        return {};

    return {index, values};
}

Result<JsonWizardFactory::Page> JsonWizardFactory::parsePage(const QVariant &value)
{
    if (value.typeId() != QMetaType::QVariantMap)
        return ResultError(Tr::tr("Page is not an object."));

    const QVariantMap data = value.toMap();
    QString defaultValueFile = data.value(QLatin1String(DEFAULT_VALUES)).toString();
    if (!defaultValueFile.isEmpty())
        defaultValueFile.append(QLatin1String(".json"));
    const QVariantMap defaultData = loadDefaultValues(defaultValueFile);

    const QString strVal = getDataValue(QLatin1String(TYPE_ID_KEY), data, defaultData).toString();
    if (strVal.isEmpty())
        return ResultError(Tr::tr("Page has no typeId set."));

    Id typeId = Id(Constants::PAGE_ID_PREFIX).withSuffix(strVal);

    JsonWizardPageFactory *factory
            = Utils::findOr(pageFactories(), nullptr, [typeId](JsonWizardPageFactory *f) { return f->canCreate(typeId); });
    if (!factory) {
        return ResultError(Tr::tr("TypeId \"%1\" of page is unknown. Supported typeIds are: \"%2\".")
                .arg(strVal)
                .arg(supportedTypeIds(pageFactories())
                    .replace(QLatin1String(Constants::PAGE_ID_PREFIX), QLatin1String())));
    }

    const QString title = JsonWizardFactory::localizedString(getDataValue(QLatin1String(DISPLAY_NAME_KEY), data, defaultData));
    const QString subTitle = JsonWizardFactory::localizedString(getDataValue(QLatin1String(PAGE_SUB_TITLE_KEY), data, defaultData));
    const QString shortTitle = JsonWizardFactory::localizedString(getDataValue(QLatin1String(PAGE_SHORT_TITLE_KEY), data, defaultData));

    bool ok;
    int index = getDataValue(QLatin1String(PAGE_INDEX_KEY), data, defaultData, -1).toInt(&ok);
    if (!ok) {
        return ResultError(Tr::tr("Page with typeId \"%1\" has invalid \"index\".")
                .arg(typeId.toString()));
    }

    QVariant enabled = getDataValue(QLatin1String(ENABLED_EXPRESSION_KEY), data, defaultData, true);
    QVariant skippable = getDataValue(QLatin1String(SKIP_FOR_SUBPROJECTS_KEY), data, defaultData,
                                      factory->defaultSkipForSubprojects());

    QVariant specifiedSubData = data.value(QLatin1String(DATA_KEY));
    QVariant defaultSubData = defaultData.value(QLatin1String(DATA_KEY));
    QVariant subData;

    if (specifiedSubData.isNull())
        subData = defaultSubData;
    else if (specifiedSubData.typeId() == QMetaType::QVariantMap)
        subData = mergeDataValueMaps(specifiedSubData.toMap(), defaultSubData.toMap());
    else if (specifiedSubData.typeId() == QMetaType::QVariantList)
        subData = specifiedSubData;

    if (auto res = factory->validateData(typeId, subData); !res)
        return ResultError(res.error());

    JsonWizardFactory::Page p;
    p.typeId = typeId;
    p.title = title;
    p.subTitle = subTitle;
    p.shortTitle = shortTitle;
    p.index = index;
    p.data = subData;
    p.enabled = enabled;
    p.skipForSubprojects = skippable;

    return p;
}

//FIXME: loadDefaultValues() has an almost identical loop. Make the loop return the results instead of
//internal processing and create a separate function for it. Then process the results in
//loadDefaultValues() and loadDefaultValues()
QList<Core::IWizardFactory *> JsonWizardFactory::createWizardFactories()
{
    QList<Core::IWizardFactory *> result;
    QString verboseLog;
    const QString wizardFileName = QLatin1String("wizard.json");

    const FilePaths paths = searchPaths();
    for (const FilePath &path : paths) {
        if (path.isEmpty())
            continue;

        if (!path.exists()) {
            if (verbose())
                verboseLog.append(
                    Tr::tr("Path \"%1\" does not exist when checking JSON wizard search paths.")
                        .arg(path.toUserOutput())
                    + "\n");
            continue;
        }

        const FileFilter filter {
            {wizardFileName}, QDir::Files|QDir::Readable|QDir::NoDotAndDotDot, QDirIterator::Subdirectories
        };
        const QDir::SortFlags sortflags = QDir::Name|QDir::IgnoreCase;
        const FilePaths wizardFiles = path.dirEntries(filter, sortflags);

        for (const FilePath &currentFile : wizardFiles) {
            QJsonParseError error;
            const QByteArray fileData = currentFile.fileContents().value_or(QByteArray());
            const QJsonDocument json = QJsonDocument::fromJson(fileData, &error);

            if (error.error != QJsonParseError::NoError) {
                int line = 1;
                int column = 1;
                for (int i = 0; i < error.offset; ++i) {
                    if (fileData.at(i) == '\n') {
                        ++line;
                        column = 1;
                    } else {
                        ++column;
                    }
                }
                verboseLog.append(
                    Tr::tr("* Failed to parse \"%1\":%2:%3: %4")
                        .arg(currentFile.fileName())
                        .arg(line)
                        .arg(column)
                        .arg(error.errorString())
                    + "\n");
                continue;
            }

            if (!json.isObject()) {
                verboseLog.append(
                    Tr::tr("* Did not find a JSON object in \"%1\".").arg(currentFile.fileName())
                    + "\n");
                continue;
            }

            if (verbose())
                verboseLog.append(Tr::tr("* Configuration found and parsed.") + "\n");

            QVariantMap data = json.object().toVariantMap();

            int version = data.value(QLatin1String(VERSION_KEY), 0).toInt();
            if (version < 1 || version > 1) {
                verboseLog.append(Tr::tr("* Version %1 not supported.").arg(version) + "\n");
                continue;
            }

            Result<JsonWizardFactory *>factory = createWizardFactory(data, currentFile.parentDir());
            if (!factory) {
                verboseLog.append(Tr::tr("* Failed to create: %1").arg(factory.error()) + "\n");
                continue;
            }

            result << *factory;
        }
    }

    if (verbose()) { // Print to output pane for Windows.
        qWarning("%s", qPrintable(verboseLog));
        Core::MessageManager::writeDisrupting(verboseLog);
    }
    return result;
}

Result<JsonWizardFactory *> JsonWizardFactory::createWizardFactory(const QVariantMap &data,
                                                                   const FilePath &baseDir)
{
    auto *factory = new JsonWizardFactory;
    if (Result<> res = factory->initialize(data, baseDir); !res) {
        delete factory;
        factory = nullptr;
        if (verbose())
            Core::MessageManager::writeDisrupting(res.error());
        return ResultError(res.error());
    }
    return factory;
}

static QStringList environmentTemplatesPaths()
{
    QStringList paths;

    QString envTempPath = qtcEnvironmentVariable("QTCREATOR_TEMPLATES_PATH");

    if (!envTempPath.isEmpty()) {
        for (const QString &path : envTempPath
             .split(HostOsInfo::pathListSeparator(), Qt::SkipEmptyParts)) {
            QString canonicalPath = QDir(path).canonicalPath();
            if (!canonicalPath.isEmpty() && !paths.contains(canonicalPath))
                paths.append(canonicalPath);
        }
    }

    return paths;
}

static bool s_searchPathsInitialized = false;
Q_GLOBAL_STATIC(FilePath, s_installedWizardsPath, {Core::ICore::resourcePath(WIZARD_PATH)})
Q_GLOBAL_STATIC(FilePaths, s_additionalWizardPaths)

FilePaths &JsonWizardFactory::searchPaths()
{
    static FilePaths m_searchPaths;
    if (!s_searchPathsInitialized) {
        s_searchPathsInitialized = true;
        m_searchPaths = {Core::ICore::userResourcePath(WIZARD_PATH), *s_installedWizardsPath};
        for (const QString &environmentTemplateDirName : environmentTemplatesPaths())
            m_searchPaths << FilePath::fromString(environmentTemplateDirName);
        m_searchPaths << Utils::transform(
            Core::ICore::settings()->value("Wizards/SearchPaths").toStringList(),
            [](const QString &s) { return FilePath::fromUserInput(s); });
        // add paths from enabled plugin meta data
        for (PluginSpec *plugin : PluginManager::plugins()) {
            if (plugin->state() == PluginSpec::Running) {
                const auto base = plugin->location();
                const auto values = plugin->metaData().value("JsonWizardPaths").toArray();
                for (const QJsonValue &v : values) {
                    const auto path = FilePath::fromString(v.toString());
                    if (!path.isEmpty() && path.isLocal())
                        m_searchPaths << base.resolvePath(path);
                }
            }
        }
        m_searchPaths += *s_additionalWizardPaths;
    }

    return m_searchPaths;
}

void JsonWizardFactory::resetSearchPaths()
{
    s_searchPathsInitialized = false;
}

void JsonWizardFactory::addWizardPath(const FilePath &path)
{
    s_additionalWizardPaths->append(path);
}

/*!
    \internal
*/
void JsonWizardFactory::setInstalledWizardsPath(const Utils::FilePath &path)
{
    *s_installedWizardsPath = path;
    resetSearchPaths();
}

void JsonWizardFactory::setVerbose(int level)
{
    m_verbose = level;
}

int JsonWizardFactory::verbose()
{
    return m_verbose;
}

static QString qmlProjectName(const FilePath &folder)
{
    FilePath currentFolder = folder;
    while (!currentFolder.isEmpty()) {
        const FilePaths fileList = currentFolder.dirEntries({{"*.qmlproject"}});
        if (!fileList.isEmpty())
            return fileList.first().baseName();
        currentFolder = currentFolder.parentDir();
    }

    return {};
}

Wizard *JsonWizardFactory::runWizardImpl(const FilePath &path, Id platform,
                                         const QVariantMap &variables, bool showWizard)
{
    auto wizard = new JsonWizard;
    wizard->setWindowIcon(icon());
    wizard->setWindowTitle(displayName());

    wizard->setValue(QStringLiteral("WizardDir"), m_wizardDir.toVariant());
    QSet<Id> tmp = requiredFeatures();
    tmp.subtract(pluginFeatures());
    wizard->setValue(QStringLiteral("RequiredFeatures"), Id::toStringList(tmp));
    tmp = m_preferredFeatures;
    tmp.subtract(pluginFeatures());
    wizard->setValue(QStringLiteral("PreferredFeatures"), Id::toStringList(tmp));

    wizard->setValue(QStringLiteral("Features"), Id::toStringList(availableFeatures(platform)));
    wizard->setValue(QStringLiteral("Plugins"), Id::toStringList(pluginFeatures()));
    wizard->setValue(QStringLiteral("SupportedProjectTypes"), Id::toStringList(supportedProjectTypes()));

    // Add data to wizard:
    for (auto i = variables.constBegin(); i != variables.constEnd(); ++i)
        wizard->setValue(i.key(), i.value());

    wizard->setValue(QStringLiteral("InitialPath"), path.toUrlishString());
    wizard->setValue(QStringLiteral("QmlProjectName"), qmlProjectName(path));
    wizard->setValue(QStringLiteral("Platform"), platform.toString());

    QString kindStr = QLatin1String(Core::Constants::WIZARD_KIND_UNKNOWN);
    if (kind() == IWizardFactory::FileWizard)
        kindStr = QLatin1String(Core::Constants::WIZARD_KIND_FILE);
    else if (kind() == IWizardFactory::ProjectWizard)
        kindStr = QLatin1String(Core::Constants::WIZARD_KIND_PROJECT);
    wizard->setValue(QStringLiteral("kind"), kindStr);

    wizard->setValue(QStringLiteral("trDescription"), description());
    wizard->setValue(QStringLiteral("trDisplayName"), displayName());
    wizard->setValue(QStringLiteral("trDisplayCategory"), displayCategory());
    wizard->setValue(QStringLiteral("category"), category());
    wizard->setValue(QStringLiteral("id"), id().toString());

    MacroExpander *expander = wizard->expander();
    for (const JsonWizard::OptionDefinition &od : std::as_const(m_options)) {
        if (od.condition(*expander))
            wizard->setValue(od.key(), od.value(*expander));
    }

    bool havePage = false;
    for (const Page &data : std::as_const(m_pages)) {
        QTC_ASSERT(data.isValid(), continue);

        if (!JsonWizard::boolFromVariant(data.enabled, wizard->expander()))
            continue;

        havePage = true;
        JsonWizardPageFactory *factory = findOr(pageFactories(), nullptr,
                                                       [&data](JsonWizardPageFactory *f) {
                                                            return f->canCreate(data.typeId);
                                                       });
        QTC_ASSERT(factory, continue);
        WizardPage *page = factory->create(wizard, data.typeId, data.data);
        QTC_ASSERT(page, continue);

        page->setTitle(data.title);
        page->setSubTitle(data.subTitle);
        page->setProperty(Utils::SHORT_TITLE_PROPERTY, data.shortTitle);
        page->setSkipForSubprojects(JsonWizard::boolFromVariant(data.skipForSubprojects,
                                                                wizard->expander()));

        if (data.index >= 0) {
            wizard->setPage(data.index, page);
            if (wizard->page(data.index) != page) // Failed to set page!
                delete page;
        } else {
            wizard->addPage(page);
        }
    }

    for (const Generator &data : std::as_const(m_generators)) {
        QTC_ASSERT(data.isValid(), continue);
        JsonWizardGeneratorFactory *factory = Utils::findOr(generatorFactories(), nullptr,
                                                            [&data](JsonWizardGeneratorFactory *f) {
                                                                 return f->canCreate(data.typeId);
                                                            });
        QTC_ASSERT(factory, continue);
        JsonWizardGenerator *gen = factory->create(data.typeId, data.data, path.toUrlishString(), platform, variables);
        QTC_ASSERT(gen, continue);

        wizard->addGenerator(gen);
    }

    if (!havePage) {
        wizard->accept();
        wizard->deleteLater();
        return nullptr;
    }

    if (showWizard)
        wizard->show();
    return wizard;
}

Result<QVariantList> JsonWizardFactory::objectOrList(const QVariant &data)
{
    if (data.isNull())
        return ResultError(Tr::tr("key not found."));
    if (data.typeId() == QMetaType::QVariantMap)
        return QVariantList{data};
    if (data.typeId() == QMetaType::QVariantList)
        return data.toList();
    return ResultError(Tr::tr("Expected an object or a list."));
}

QString JsonWizardFactory::localizedString(const QVariant &value)
{
    if (value.isNull())
        return {};
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap tmp = value.toMap();
        const QString currentLocale = languageSetting().toLower();
        const QStringList locales{currentLocale, "en", "C"};
        for (const QString &locale : locales) {
            QString result = tmp.value(locale, QString()).toString();
            if (!result.isEmpty())
                return result;
        }
        return {};
    }
    return Tr::tr(value.toByteArray());
}

bool JsonWizardFactory::isAvailable(Id platformId) const
{
    if (!IWizardFactory::isAvailable(platformId)) // check for required features
        return false;

    MacroExpander expander;
    MacroExpander *e = &expander;
    expander.registerVariable("Platform", Tr::tr("The platform selected for the wizard."),
                              [platformId]() { return platformId.toString(); });
    expander.registerVariable("Features", Tr::tr("The features available to this wizard."),
                              [e, platformId]() { return JsonWizard::stringListToArrayString(Id::toStringList(availableFeatures(platformId)), e); });
    expander.registerVariable("Plugins", Tr::tr("The plugins loaded."), [e]() {
        return JsonWizard::stringListToArrayString(Id::toStringList(pluginFeatures()), e);
    });
    Core::JsExpander jsExpander;
    jsExpander.registerObject("Wizard",
                              new Internal::JsonWizardFactoryJsExtension(platformId,
                                                                         availableFeatures(
                                                                             platformId),
                                                                         pluginFeatures()));
    jsExpander.evaluate("var value = Wizard.value");
    jsExpander.evaluate("var isPluginRunning = Wizard.isPluginRunning");
    jsExpander.evaluate("var isAnyPluginRunning = Wizard.isAnyPluginRunning");

    jsExpander.registerForExpander(e);
    return JsonWizard::boolFromVariant(m_enabledExpression, &expander);
}

Result<> JsonWizardFactory::initialize(const QVariantMap &data, const FilePath &baseDir)
{
    m_wizardDir = baseDir.absoluteFilePath();

    m_enabledExpression = data.value(QLatin1String(ENABLED_EXPRESSION_KEY), true);

    QSet<Id> projectTypes = Id::fromStringList(data.value(QLatin1String(SUPPORTED_PROJECTS)).toStringList());
    // FIXME: "kind" was relevant up to and including Qt Creator 3.6:
    const QString unsetKind = QUuid::createUuid().toString();
    QString strVal = data.value(QLatin1String(KIND_KEY), unsetKind).toString();
    if (strVal != unsetKind
            && strVal != QLatin1String("class")
            && strVal != QLatin1String("file")
            && strVal != QLatin1String("project")) {
        return ResultError(
            Tr::tr("\"kind\" value \"%1\" is not \"class\" (deprecated), \"file\" or \"project\".").arg(strVal));
    }
    if ((strVal == QLatin1String("file") || strVal == QLatin1String("class")) && !projectTypes.isEmpty()) {
        return ResultError(
            Tr::tr("\"kind\" is \"file\" or \"class\" (deprecated) and \"%1\" is also set.")
                .arg(QLatin1String(SUPPORTED_PROJECTS)));
    }
    if (strVal == QLatin1String("project") && projectTypes.isEmpty())
        projectTypes.insert("UNKNOWN_PROJECT");
    // end of legacy code
    setSupportedProjectTypes(projectTypes);

    strVal = data.value(QLatin1String(ID_KEY)).toString();
    if (strVal.isEmpty())
        return ResultError(Tr::tr("No id set."));

    setId(Id::fromString(strVal));

    strVal = data.value(QLatin1String(CATEGORY_KEY)).toString();
    if (strVal.isEmpty())
        return ResultError(Tr::tr("No category is set."));

    setCategory(strVal);

    FilePath iconPath;
    if (data.contains(QLatin1String(ICON_KEY))) {
        strVal = data.value(QLatin1String(ICON_KEY)).toString();
        iconPath = baseDir.resolvePath(strVal);
        if (!iconPath.exists())
            return ResultError(Tr::tr("Icon file \"%1\" not found.").arg(iconPath.toUserOutput()));
    }

    const QString iconText = data.value(QLatin1String(ICON_TEXT_KEY)).toString();
    const bool iconIsThemed = data.value(QLatin1String(ICON_KIND_KEY)).toString()
            .compare("Themed", Qt::CaseInsensitive) == 0;
    setIcon(iconIsThemed ? themedIcon(iconPath)
                         : strVal.isEmpty() ? QIcon()
                                            : QIcon(iconPath.toUrlishString()),
            iconText);

    const QString fontIconName = data.value(QLatin1String(FONT_ICON_NAME_KEY)).toString();
    setFontIconName(fontIconName);

    strVal = data.value(QLatin1String(IMAGE_KEY)).toString();
    if (!strVal.isEmpty()) {
        const FilePath imagePath = baseDir.resolvePath(strVal);
        if (!imagePath.exists())
            return ResultError(Tr::tr("Image file \"%1\" not found.").arg(imagePath.toUserOutput()));

        setDescriptionImage(imagePath.toUrlishString());
    }

    const FilePath detailsPage = baseDir.resolvePath(QString("detailsPage.qml"));
    if (detailsPage.exists())
        setDetailsPageQmlPath(detailsPage.toUrlishString());

    setRequiredFeatures(Id::fromStringList(data.value(QLatin1String(REQUIRED_FEATURES_KEY)).toStringList()));
    m_preferredFeatures = Id::fromStringList(data.value(QLatin1String(SUGGESTED_FEATURES_KEY)).toStringList());
    m_preferredFeatures.unite(requiredFeatures());

    strVal = localizedString(data.value(QLatin1String(DISPLAY_NAME_KEY)));
    if (strVal.isEmpty())
        return ResultError(Tr::tr("No displayName set."));

    setDisplayName(strVal);

    strVal = localizedString(data.value(QLatin1String(CATEGORY_NAME_KEY)));
    if (strVal.isEmpty())
        return ResultError(Tr::tr("No displayCategory set."));

    setDisplayCategory(strVal);

    strVal = localizedString(data.value(QLatin1String(DESCRIPTION_KEY)));
    if (strVal.isEmpty())
        return ResultError(Tr::tr("No description set."));

    setDescription(strVal);

    // Generator:
    Result<QVariantList> list = objectOrList(data.value(QLatin1String(GENERATOR_KEY)));
    if (!list)
        return ResultError(Tr::tr("When parsing \"generators\": %1").arg(list.error()));

    for (const QVariant &v : std::as_const(*list)) {
        Result<Generator> gen = parseGenerator(v);
        if (!gen)
            return ResultError(gen.error());
        m_generators.append(*gen);
    }

    // Pages:
    list = objectOrList(data.value(QLatin1String(PAGES_KEY)));
    if (!list)
        return ResultError(list.error());

    for (const QVariant &v : std::as_const(*list)) {
        Result<Page> p = parsePage(v);
        if (!p)
            return ResultError(p.error());
        if (!p->isValid())
            return ResultError(Tr::tr("No valid typeId"));
        m_pages.append(*p);
    }

    WizardFlags flags;
    if (data.value(QLatin1String(PLATFORM_INDEPENDENT_KEY), false).toBool())
        flags |= PlatformIndependent;
    setFlags(flags);

    // Options:
    Result<JsonWizard::OptionDefinitions> res =
        JsonWizard::parseOptions(data.value(QLatin1String(OPTIONS_KEY)));
    if (!res)
        return ResultError(res.error());

    m_options = *res;
    return ResultOk;
}

} // namespace ProjectExplorer

#include "jsonwizardfactory.moc"
