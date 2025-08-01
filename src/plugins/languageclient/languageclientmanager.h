// Copyright (C) 2018 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "client.h"
#include "languageclient_global.h"
#include "languageclientsettings.h"
#include "lspinspector.h"

#include <utils/algorithm.h>
#include <utils/id.h>

#include <languageserverprotocol/diagnostics.h>
#include <languageserverprotocol/languagefeatures.h>
#include <languageserverprotocol/textsynchronization.h>

namespace Core {
class IEditor;
class IDocument;
}

namespace ProjectExplorer {
class BuildConfiguration;
class Project;
}

namespace LanguageClient {

class LanguageClientManagerPrivate;
class LanguageClientMark;

class LANGUAGECLIENT_EXPORT LanguageClientManager : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LanguageClientManager)

public:
    ~LanguageClientManager() override;

    static void clientStarted(Client *client);
    static void clientFinished(Client *client);
    static Client *startClient(const BaseSettings *setting, ProjectExplorer::BuildConfiguration *bc = nullptr);
    static const QList<Client *> clients();
    static void addClient(Client *client);
    static void restartClient(Client *client);

    static void shutdownClient(Client *client);
    static void deleteClient(Client *client, bool unexpected = false);

    static void shutdown();
    static bool isShutdownFinished();

    static LanguageClientManager *instance();

    static QList<Client *> clientsSupportingDocument(
        const TextEditor::TextDocument *doc, bool onlyReachable = true);

    static void applySettings();
    static void applySettings(const QString &settingsId);
    static void applySettings(BaseSettings *settings);
    static void writeSettings();
    static QList<BaseSettings *> currentSettings();
    static void registerClientSettings(BaseSettings *settings);
    static void enableClientSettings(const QString &settingsId, bool enable = true);
    static QList<Client *> clientsForSetting(const BaseSettings *setting);
    static QList<Client *> clientsForSettingId(const QString &settingsId);
    static const BaseSettings *settingForClient(Client *setting);
    static QList<Client *> clientsByName(const QString &name);
    static void updateWorkspaceConfiguration(const ProjectExplorer::Project *project,
                                             const QJsonValue &json);

    static Client *clientForDocument(TextEditor::TextDocument *document);
    static Client *clientForFilePath(const Utils::FilePath &filePath);
    static const QList<Client *> clientsForBuildConfiguration(const ProjectExplorer::BuildConfiguration *bc);

    template<typename T> static bool hasClients();

    ///
    /// \brief openDocumentWithClient
    /// makes sure the document is opened and activated with the client and
    /// deactivates the document for a potential previous active client
    ///
    static void openDocumentWithClient(TextEditor::TextDocument *document, Client *client);

    static void logJsonRpcMessage(const LspLogMessage::MessageSender sender,
                                  const QString &clientName,
                                  const LanguageServerProtocol::JsonRpcMessage &message);

    static void showInspector();

public slots:
    // These slots are called automatically if the a file is opened via the usual EditorManager
    // methods. If you create an editor manually, you need to call these slots manually as well.
    void editorOpened(Core::IEditor *editor);
    void documentOpened(Core::IDocument *document);
    void documentClosed(Core::IDocument *document);

signals:
    void clientAdded(Client *client);
    void clientInitialized(Client *client);
    void clientRemoved(Client *client, bool unexpected);
    void shutdownFinished();
    void openCallHierarchy();

private:
    LanguageClientManager();

    friend void setupLanguageClientManager();

    void updateProject(ProjectExplorer::BuildConfiguration *bc);
    void buildConfigurationAdded(ProjectExplorer::BuildConfiguration *bc);

    void trackClientDeletion(Client *client);

    void documentOpenedForProject(
        TextEditor::TextDocument *textDocument,
        BaseSettings *setting,
        const QList<Client *> &clients);

    QList<Client *> reachableClients();

    QList<Client *> m_clients;
    QSet<Client *> m_restartingClients;
    QList<BaseSettings *>  m_currentSettings; // owned
    QMap<QString, QList<Client *>> m_clientsForSetting;
    QHash<TextEditor::TextDocument *, QPointer<Client>> m_clientForDocument;
    std::unique_ptr<LanguageClientManagerPrivate> d;
    LspInspector m_inspector;
    QSet<Utils::Id> m_scheduledForDeletion;
};

template<typename T> bool LanguageClientManager::hasClients()
{
    return Utils::contains(instance()->m_clients, [](const Client *c) {
        return qobject_cast<const T* >(c);
    });
}

void setupLanguageClientManager();

} // namespace LanguageClient
