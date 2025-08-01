// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "extensionsystem_global.h"

#include <aggregation/aggregate.h>
#include <utils/filepath.h>
#include <utils/qtcsettings.h>

#include <QObject>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QTextStream;
QT_END_NAMESPACE

namespace ExtensionSystem {
class IPlugin;
class PluginSpec;

namespace Internal { class PluginManagerPrivate; }

class EXTENSIONSYSTEM_EXPORT PluginManager : public QObject
{
    Q_OBJECT

public:
    static PluginManager *instance();

    PluginManager();
    ~PluginManager() override;

    // Object pool operations
    static void addObject(QObject *obj);
    static void removeObject(QObject *obj);
    static QObjectList allObjects();
    static QReadWriteLock *listLock();

    // This is useful for soft dependencies using pure interfaces.
    template <typename T> static T *getObject()
    {
        QReadLocker lock(listLock());
        const QObjectList all = allObjects();
        for (QObject *obj : all) {
            if (T *result = qobject_cast<T *>(obj))
                return result;
        }
        return nullptr;
    }
    template <typename T, typename Predicate> static T *getObject(Predicate predicate)
    {
        QReadLocker lock(listLock());
        const QObjectList all = allObjects();
        for (QObject *obj : all) {
            if (T *result = qobject_cast<T *>(obj))
                if (predicate(result))
                    return result;
        }
        return 0;
    }

    static QObject *getObjectByName(const QString &name);

    static void startProfiling();
    // Plugin operations
    static QList<PluginSpec *> loadQueue();
    static void loadPlugins();
    static void loadPluginsAtRuntime(const QSet<PluginSpec *> &plugins);
    static Utils::FilePaths pluginPaths();
    static void setPluginPaths(const Utils::FilePaths &paths);
    static QString pluginIID();
    static void setPluginIID(const QString &iid);
    static const QList<PluginSpec *> plugins();
    static QHash<QString, QList<PluginSpec *>> pluginCollections();
    static bool hasError();
    static const QStringList allErrors();
    static const QSet<PluginSpec *> pluginsRequiringPlugin(PluginSpec *spec);
    static const QSet<PluginSpec *> pluginsToEnableForPlugin(PluginSpec *spec);
    static void checkForProblematicPlugins();
    static PluginSpec *specForPlugin(IPlugin *plugin);
    static PluginSpec *specById(const QString &id);
    static bool specExists(const QString &id);
    static bool specExistsAndIsEnabled(const QString &id);

    static void addPlugins(const QList<PluginSpec *> &specs);

    static void reInstallPlugins();

    static Utils::Result<> removePluginOnRestart(const QString &id);
    static void installPluginOnRestart(
        const Utils::FilePath &source, const Utils::FilePath &destination);

    static void removePluginsAfterRestart();
    static void installPluginsAfterRestart();

    // UI
    static std::optional<QSet<PluginSpec *>> askForEnablingPlugins(
        QWidget *dialogParent, const QSet<PluginSpec *> &plugins, bool enable);

    // Settings
    static Utils::QtcSettings *settings();
    static Utils::QtcSettings *globalSettings();
    static void writeSettings();

    // command line arguments
    static QStringList arguments();
    static QStringList argumentsForRestart();
    static Utils::Result<> parseOptions(const QStringList &args,
        const QMap<QString, bool> &appOptions,
        QMap<QString, QString> *foundAppOptions);
    static void formatOptions(QTextStream &str, int optionIndentation, int descriptionIndentation);
    static void formatPluginOptions(QTextStream &str, int optionIndentation, int descriptionIndentation);
    static void formatPluginVersions(QTextStream &str);

    static QString serializedArguments();

    static bool testRunRequested();

#ifdef EXTENSIONSYSTEM_WITH_TESTOPTION
    static bool registerScenario(const QString &scenarioId, std::function<bool()> scenarioStarter);
    static bool isScenarioRequested();
    static bool runScenario();
    static bool isScenarioRunning(const QString &scenarioId);
    // static void triggerScenarioPoint(const QVariant pointData); // ?? called from scenario point
    static bool finishScenario();
    static void waitForScenarioFullyInitialized();
    // signals:
    // void scenarioPointTriggered(const QVariant pointData); // ?? e.g. in StringTable::GC() -> post a call to quit into main thread and sleep for 5 seconds in the GC thread
#endif

    struct ProcessData {
        QString m_executable;
        QStringList m_args;
        QString m_workingPath;
        QString m_settingsPath;
    };

    static void setCreatorProcessData(const ProcessData &data);
    static ProcessData creatorProcessData();

    static QString platformName();

    static bool isInitializationDone();
    static bool isShuttingDown();

    static void remoteArguments(const QString &serializedArguments, QObject *socket);
    static void shutdown();

    static QString systemInformation();

    void setAcceptTermsAndConditionsCallback(const std::function<bool(PluginSpec *)> &callback);
    void setTermsAndConditionsAccepted(PluginSpec *spec);

signals:
    void objectAdded(QObject *obj);
    void aboutToRemoveObject(QObject *obj);

    void pluginsChanged();
    void initializationDone();
    void testsFinished(int failedTests);
    void scenarioFinished(int exitCode);

    friend class Internal::PluginManagerPrivate;
};

} // namespace ExtensionSystem
