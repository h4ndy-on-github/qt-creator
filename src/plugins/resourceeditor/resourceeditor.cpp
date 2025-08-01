// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "resourceeditor.h"

#include "projectexplorer/projectexplorerconstants.h"
#include "resourceeditorconstants.h"
#include "resourceeditortr.h"
#include "qrceditor/qrceditor.h"
#include "qrceditor/resourcefile_p.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreplugintr.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/idocument.h>

#include <utils/fileutils.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/mimeconstants.h>
#include <utils/reloadpromptutils.h>
#include <utils/stringutils.h>

#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>

using namespace Core;
using namespace Utils;

namespace ResourceEditor::Internal {

enum { debugResourceEditorW = 0 };

static QAction *s_redoAction = nullptr;
static QAction *s_undoAction = nullptr;
static QAction *s_refreshAction = nullptr;

class ResourceEditorDocument final : public IDocument
{
    Q_OBJECT
    Q_PROPERTY(QString plainText READ plainText STORED false) // For access by code pasters

public:
    ResourceEditorDocument(QObject *parent = nullptr);

    Result<> open(const FilePath &filePath, const FilePath &realFilePath) final;
    QString plainText() const { return m_model.contents(); }
    QByteArray contents() const final { return m_model.contents().toUtf8(); }
    Result<> setContents(const QByteArray &contents) final;
    bool shouldAutoSave() const final { return m_shouldAutoSave; }
    bool isModified() const final { return m_model.dirty(); }
    bool isSaveAsAllowed() const final { return true; }
    Result<> reload(ReloadFlag flag, ChangeType type) final;
    void setFilePath(const FilePath &newName) final;
    void setBlockDirtyChanged(bool value) { m_blockDirtyChanged = value; }

    RelativeResourceModel *model() { return &m_model; }
    void setShouldAutoSave(bool save) { m_shouldAutoSave = save; }

signals:
    void loaded(bool success);

private:
    Result<> saveImpl(const FilePath &filePath, bool autoSave) final;
    void dirtyChanged(bool);

    RelativeResourceModel m_model;
    bool m_blockDirtyChanged = false;
    bool m_shouldAutoSave = false;
};

ResourceEditorDocument::ResourceEditorDocument(QObject *parent)
    : IDocument(parent)
{
    setId(ResourceEditor::Constants::RESOURCEEDITOR_ID);
    setMimeType(Utils::Constants::RESOURCE_MIMETYPE);
    connect(&m_model, &RelativeResourceModel::dirtyChanged,
            this, &ResourceEditorDocument::dirtyChanged);
    connect(&m_model, &ResourceModel::contentsChanged,
            this, &IDocument::contentsChanged);

    if (debugResourceEditorW)
        qDebug() <<  "ResourceEditorFile::ResourceEditorFile()";
}

class ResourceEditorImpl final : public IEditor
{
    Q_OBJECT

public:
    ResourceEditorImpl();
    ~ResourceEditorImpl() final;

    static QrcEditor *currentQrcEditor()
    {
        auto const focusEditor = qobject_cast<ResourceEditorImpl *>(EditorManager::currentEditor());
        QTC_ASSERT(focusEditor, return nullptr);
        return focusEditor->m_resourceEditor;
    }

    IDocument *document() const final { return m_resourceDocument; }
    QWidget *toolBar() final { return m_toolBar; }
    QByteArray saveState() const final;
    void restoreState(const QByteArray &state) final;

private:
    void onUndoStackChanged(bool canUndo, bool canRedo);
    void showContextMenu(const QPoint &globalPoint, const FilePath &filePath);
    void openCurrentFile();
    void openFile(const FilePath &filePath);
    void renameCurrentFile();
    void copyCurrentResourcePath();
    void orderList();

    const QString m_extension;
    const QString m_fileFilter;
    QString m_displayName;
    QrcEditor *m_resourceEditor;
    ResourceEditorDocument *m_resourceDocument;
    QMenu *m_contextMenu;
    QMenu *m_openWithMenu;
    FilePath m_currentFilePath;
    QToolBar *m_toolBar;
    QAction *m_renameAction;
    QAction *m_copyFileNameAction;
    QAction *m_orderList;

    friend class ResourceEditorDocument;
};

ResourceEditorImpl::ResourceEditorImpl()
    : m_resourceDocument(new ResourceEditorDocument(this)),
    m_contextMenu(new QMenu),
    m_toolBar(new QToolBar)
{
    m_resourceEditor = new QrcEditor(m_resourceDocument->model(), nullptr);

    setContext(Context(Constants::C_RESOURCEEDITOR));
    setWidget(m_resourceEditor);

    QToolButton *refreshButton = Command::createToolButtonWithShortcutToolTip(Constants::REFRESH);
    refreshButton->defaultAction()->setIcon(QIcon(":/texteditor/images/finddocuments.png"));
    connect(refreshButton, &QAbstractButton::clicked, m_resourceEditor, &QrcEditor::refresh);
    m_toolBar->addWidget(refreshButton);

    m_resourceEditor->setResourceDragEnabled(true);
    m_contextMenu->addAction(Tr::tr("Open File"), this, &ResourceEditorImpl::openCurrentFile);
    m_openWithMenu = m_contextMenu->addMenu(Tr::tr("Open With"));
    m_renameAction = m_contextMenu->addAction(Tr::tr("Rename File..."), this,
                                              &ResourceEditorImpl::renameCurrentFile);
    m_copyFileNameAction = m_contextMenu->addAction(Tr::tr("Copy Resource Path to Clipboard"),
                                                    this, &ResourceEditorImpl::copyCurrentResourcePath);
    m_orderList = m_contextMenu->addAction(Tr::tr("Sort Alphabetically"), this, &ResourceEditorImpl::orderList);

    connect(m_resourceDocument, &ResourceEditorDocument::loaded,
            m_resourceEditor, &QrcEditor::loaded);
    connect(m_resourceEditor, &QrcEditor::undoStackChanged,
            this, &ResourceEditorImpl::onUndoStackChanged);
    connect(m_resourceEditor, &QrcEditor::showContextMenu,
            this, &ResourceEditorImpl::showContextMenu);
    connect(m_resourceEditor, &QrcEditor::itemActivated,
            this, &ResourceEditorImpl::openFile);
    connect(m_resourceEditor->commandHistory(), &QUndoStack::indexChanged,
            m_resourceDocument, [this] { m_resourceDocument->setShouldAutoSave(true); });
    if (debugResourceEditorW)
        qDebug() <<  "ResourceEditorW::ResourceEditorW()";
}

ResourceEditorImpl::~ResourceEditorImpl()
{
    if (m_resourceEditor)
        m_resourceEditor->deleteLater();
    delete m_contextMenu;
    delete m_toolBar;
}

Result<> ResourceEditorDocument::open(const FilePath &filePath, const FilePath &realFilePath)
{
    if (debugResourceEditorW)
        qDebug() <<  "ResourceEditorW::open: " << filePath;

    setBlockDirtyChanged(true);

    m_model.setFilePath(realFilePath);

    Result<> openResult = m_model.reload();
    if (!openResult) {
        openResult = ResultError(m_model.errorMessage()); // FIXME: Move to m_model
        setBlockDirtyChanged(false);
        emit loaded(false);
        return openResult;
    }

    setFilePath(filePath);
    setBlockDirtyChanged(false);
    m_model.setDirty(filePath != realFilePath);
    m_shouldAutoSave = false;

    emit loaded(true);
    return ResultOk;
}

Result<> ResourceEditorDocument::saveImpl(const FilePath &filePath, bool autoSave)
{
    if (debugResourceEditorW)
        qDebug() << ">ResourceEditorW::saveImpl: " << filePath;

    if (filePath.isEmpty())
        return ResultError("ASSERT: ResourceEditorDocument: filePath.isEmpty()");

    m_blockDirtyChanged = true;
    m_model.setFilePath(filePath);
    if (!m_model.save()) {
        m_model.setFilePath(this->filePath());
        m_blockDirtyChanged = false;
        return ResultError(m_model.errorMessage());
    }

    m_shouldAutoSave = false;
    if (autoSave) {
        m_model.setFilePath(this->filePath());
        m_model.setDirty(true);
        m_blockDirtyChanged = false;
        return ResultOk;
    }

    setFilePath(filePath);
    m_blockDirtyChanged = false;

    emit changed();
    return ResultOk;
}

Result<> ResourceEditorDocument::setContents(const QByteArray &contents)
{
    TempFileSaver saver;
    saver.write(contents);
    if (const Result<> res = saver.finalize(); !res) {
        FileUtils::showError(res.error());
        return res;
    }

    const FilePath originalFileName = m_model.filePath();
    m_model.setFilePath(saver.filePath());
    const Result<> result = m_model.reload();
    const bool success = result.has_value();
    m_model.setFilePath(originalFileName);
    m_shouldAutoSave = false;
    if (debugResourceEditorW)
        qDebug() <<  "ResourceEditorW::createNew: " << contents << " (" << saver.filePath() << ") returns " << success;
    emit loaded(success);
    return result;
}

void ResourceEditorDocument::setFilePath(const FilePath &newName)
{
    m_model.setFilePath(newName);
    IDocument::setFilePath(newName);
}

QByteArray ResourceEditorImpl::saveState() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << m_resourceEditor->saveState();
    return bytes;
}

void ResourceEditorImpl::restoreState(const QByteArray &state)
{
    QDataStream stream(state);
    QByteArray splitterState;
    stream >> splitterState;
    m_resourceEditor->restoreState(splitterState);
}

Result<> ResourceEditorDocument::reload(ReloadFlag flag, ChangeType type)
{
    Q_UNUSED(type)
    if (flag == FlagIgnore)
        return ResultOk;
    emit aboutToReload();
    const Result<> result = open(filePath(), filePath());
    emit reloadFinished(result.has_value());
    return result;
}

void ResourceEditorDocument::dirtyChanged(bool dirty)
{
    if (m_blockDirtyChanged)
        return; // We emit changed() afterwards, unless it was an autosave

    if (debugResourceEditorW)
        qDebug() << " ResourceEditorW::dirtyChanged" <<  dirty;
    emit changed();
}

void ResourceEditorImpl::onUndoStackChanged(bool canUndo, bool canRedo)
{
    if (currentQrcEditor() == m_resourceEditor) {
        s_undoAction->setEnabled(canUndo);
        s_redoAction->setEnabled(canRedo);
    }
}

void ResourceEditorImpl::showContextMenu(const QPoint &globalPoint, const FilePath &filePath)
{
    EditorManager::populateOpenWithMenu(m_openWithMenu, filePath);
    m_currentFilePath = filePath;
    m_renameAction->setEnabled(!document()->isFileReadOnly());
    m_contextMenu->popup(globalPoint);
}

void ResourceEditorImpl::openCurrentFile()
{
    openFile(m_currentFilePath);
}

void ResourceEditorImpl::openFile(const FilePath &filePath)
{
    EditorManager::openEditor(filePath);
}

void ResourceEditorImpl::renameCurrentFile()
{
    m_resourceEditor->editCurrentItem();
}

void ResourceEditorImpl::copyCurrentResourcePath()
{
    setClipboardAndSelection(m_resourceEditor->currentResourcePath());
}

void ResourceEditorImpl::orderList()
{
    m_resourceDocument->model()->orderList();
}

class ResourceEditorFactory final : public IEditorFactory
{
public:
    ResourceEditorFactory()
    {
        setId(Constants::RESOURCEEDITOR_ID);
        setMimeTypes(QStringList(Utils::Constants::RESOURCE_MIMETYPE));
        setDisplayName(::Core::Tr::tr(Constants::C_RESOURCEEDITOR_DISPLAY_NAME));

        FileIconProvider::registerIconOverlayForSuffix(
            ProjectExplorer::Constants::FILEOVERLAY_QRC, "qrc");

        setEditorCreator([] { return new ResourceEditorImpl; });
    }
};

void setupResourceEditor(QObject *guard)
{
    static ResourceEditorFactory theResourceEditorFactory;

    // Register undo and redo
    const Context context(Constants::C_RESOURCEEDITOR);

    ActionBuilder(guard, Core::Constants::UNDO)
        .setText(Tr::tr("&Undo"))
        .bindContextAction(&s_undoAction)
        .setContext(context)
        .addOnTriggered(guard, [] {
            if (QrcEditor *editor = ResourceEditorImpl::currentQrcEditor())
                editor->onUndo();
        });

    ActionBuilder(guard, Core::Constants::REDO)
        .bindContextAction(&s_redoAction)
        .setText(Tr::tr("&Redo"))
        .setContext(context)
        .addOnTriggered(guard, [] {
            if (QrcEditor *editor = ResourceEditorImpl::currentQrcEditor())
                editor->onRedo();
        });

    ActionBuilder(guard, Constants::REFRESH)
        .setText(Tr::tr("Recheck Existence of Referenced Files"))
        .bindContextAction(&s_refreshAction)
        .setContext(context)
        .addOnTriggered(guard, [] {
            if (QrcEditor *editor = ResourceEditorImpl::currentQrcEditor())
                editor->refresh();
        });
}

} // ResourceEditor::Internal

#include "resourceeditor.moc"
