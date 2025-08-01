// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "logchangedialog.h"

#include "gitclient.h"
#include "gittr.h"

#include <vcsbase/vcsoutputwindow.h>

#include <utils/qtcassert.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QItemSelectionModel>
#include <QLabel>
#include <QModelIndex>
#include <QPainter>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

using namespace Utils;
using namespace VcsBase;

namespace Git::Internal {

enum Columns
{
    HashColumn,
    SubjectColumn,
    ColumnCount
};

class LogChangeModel : public QStandardItemModel
{
public:
    explicit LogChangeModel(LogChangeWidget *parent) : QStandardItemModel(0, ColumnCount, parent) {}

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::ToolTipRole) {
            const QString revision = index.sibling(index.row(), HashColumn).data(Qt::EditRole).toString();
            const auto it = m_descriptions.constFind(revision);
            if (it != m_descriptions.constEnd())
                return *it;
            const QString desc = QString::fromUtf8(gitClient().synchronousShow(
                                 m_workingDirectory, revision, RunFlags::NoOutput));
            m_descriptions[revision] = desc;
            return desc;
        }
        return QStandardItemModel::data(index, role);
    }

    void setWorkingDirectory(const FilePath &workingDir) { m_workingDirectory = workingDir; }
private:
    FilePath m_workingDirectory;
    mutable QHash<QString, QString> m_descriptions;
};

LogChangeWidget::LogChangeWidget(QWidget *parent)
    : Utils::TreeView(parent)
    , m_model(new LogChangeModel(this))
    , m_hasCustomDelegate(false)
{
    const QStringList headers = {Tr::tr("Hash"), Tr::tr("Subject")};
    m_model->setHorizontalHeaderLabels(headers);
    setModel(m_model);
    setMinimumWidth(300);
    setRootIsDecorated(false);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setActivationMode(Utils::DoubleClickActivation);
    connect(this, &LogChangeWidget::activated, this, &LogChangeWidget::emitCommitActivated);
    QTimer::singleShot(0, this, [this] { setFocus(); });
}

bool LogChangeWidget::init(const FilePath &repository, const QString &commit, LogFlags flags)
{
    m_model->setWorkingDirectory(repository);
    if (!populateLog(repository, commit, flags))
        return false;
    if (m_model->rowCount() > 0)
        return true;
    if (!(flags & Silent))
        VcsOutputWindow::appendError(repository, GitClient::msgNoCommits(flags & IncludeRemotes));
    return false;
}

QString LogChangeWidget::commit() const
{
    if (const QStandardItem *hashItem = currentItem(HashColumn))
        return hashItem->text();
    return {};
}

int LogChangeWidget::commitIndex() const
{
    const QModelIndex currentIndex = selectionModel()->currentIndex();
    if (currentIndex.isValid())
        return currentIndex.row();
    return -1;
}

/**
 * Returns a commit range suitable for `git format-patch`.
 *
 * The format is {"-n", "hash"} or an empty string list if nothing was selected.
 */
QStringList LogChangeWidget::patchRange() const
{
    const QModelIndexList selected = selectionModel()->selectedRows();
    if (selected.isEmpty())
        return {};

    const QString size = QString::number(selected.size());
    const QStandardItem *highestItem = m_model->item(selected.first().row());
    QTC_ASSERT(highestItem, return {});
    const QString highestText = highestItem->text();
    const QStringList result = {"-" + size, highestText};
    return result;
}

bool LogChangeWidget::isRowSelected(int row) const
{
    return selectionModel()->isRowSelected(row);
}

QString LogChangeWidget::earliestCommit() const
{
    int rows = m_model->rowCount();
    if (rows) {
        if (const QStandardItem *item = m_model->item(rows - 1, HashColumn))
            return item->text();
    }
    return {};
}

void LogChangeWidget::setItemDelegate(QAbstractItemDelegate *delegate)
{
    Utils::TreeView::setItemDelegate(delegate);
    m_hasCustomDelegate = true;
}

void LogChangeWidget::emitCommitActivated(const QModelIndex &index)
{
    if (index.isValid()) {
        const QString commit = index.sibling(index.row(), HashColumn).data().toString();
        if (!commit.isEmpty())
            emit commitActivated(commit);
    }
}

void LogChangeWidget::selectionChanged(const QItemSelection &selected,
                                       const QItemSelection &deselected)
{
    Utils::TreeView::selectionChanged(selected, deselected);
    if (!m_hasCustomDelegate)
        return;
    const QModelIndexList previousIndexes = deselected.indexes();
    if (previousIndexes.isEmpty())
        return;
    const QModelIndex current = currentIndex();
    int row = current.row();
    int previousRow = previousIndexes.first().row();
    if (row < previousRow)
        qSwap(row, previousRow);
    for (int r = previousRow; r <= row; ++r) {
        update(current.sibling(r, 0));
        update(current.sibling(r, 1));
    }
}

bool LogChangeWidget::populateLog(const FilePath &repository, const QString &commit, LogFlags flags)
{
    const QString currentCommit = this->commit();
    int selected = currentCommit.isEmpty() ? 0 : -1;
    if (const int rowCount = m_model->rowCount())
        m_model->removeRows(0, rowCount);

    // Retrieve log using a custom format "Hash:Subject [(refs)]"
    QStringList arguments;
    arguments << "--max-count=1000" << "--format=%h:%s %d";
    arguments << (commit.isEmpty() ? "HEAD" : commit);
    if (!(flags & IncludeRemotes)) {
        QString remotesFlag("--remotes");
        if (!m_excludedRemote.isEmpty())
            remotesFlag += '=' + m_excludedRemote;
        arguments << "--not" << remotesFlag;
    }
    arguments << "--";

    const Result<QString> res = gitClient().synchronousLog(repository, arguments, RunFlags::NoOutput);
    if (!res) {
        VcsOutputWindow::appendError(repository, res.error());
        return false;
    }

    const QStringList lines = res.value().split('\n');
    for (const QString &line : lines) {
        const int colonPos = line.indexOf(':');
        if (colonPos != -1) {
            QList<QStandardItem *> row;
            for (int c = 0; c < ColumnCount; ++c) {
                auto item = new QStandardItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (line.endsWith(')')) {
                    QFont font = item->font();
                    font.setBold(true);
                    item->setFont(font);
                }
                row.push_back(item);
            }
            const QString hash = line.left(colonPos);
            row[HashColumn]->setText(hash);
            row[SubjectColumn]->setText(line.right(line.size() - colonPos - 1));
            m_model->appendRow(row);
            if (selected == -1 && currentCommit == hash)
                selected = m_model->rowCount() - 1;
        }
    }
    setCurrentIndex(m_model->index(selected, 0));
    return true;
}

const QStandardItem *LogChangeWidget::currentItem(int column) const
{
    const QModelIndex currentIndex = selectionModel()->currentIndex();
    if (currentIndex.isValid())
        return m_model->item(currentIndex.row(), column);
    return nullptr;
}

LogChangeDialog::LogChangeDialog(DialogType type, QWidget *parent) :
    QDialog(parent)
    , m_widget(new LogChangeWidget)
    , m_dialogButtonBox(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    const bool isReset = type == Reset;
    auto layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(isReset ? Tr::tr("Reset to:") : Tr::tr("Select change:"), this));
    layout->addWidget(m_widget);
    auto popUpLayout = new QHBoxLayout;
    if (isReset) {
        popUpLayout->addWidget(new QLabel(Tr::tr("Reset type:")));
        m_resetTypeComboBox = new QComboBox;
        m_resetTypeComboBox->addItem(Tr::tr("Hard"), "--hard");
        m_resetTypeComboBox->addItem(Tr::tr("Mixed"), "--mixed");
        m_resetTypeComboBox->addItem(Tr::tr("Soft"), "--soft");
        m_resetTypeComboBox->setCurrentIndex(settings().lastResetIndex());
        popUpLayout->addWidget(m_resetTypeComboBox);
        popUpLayout->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Ignored));
    }

    popUpLayout->addWidget(m_dialogButtonBox);
    QPushButton *okButton = m_dialogButtonBox->button(QDialogButtonBox::Ok);
    layout->addLayout(popUpLayout);

    connect(m_dialogButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_dialogButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_widget, &LogChangeWidget::activated, okButton, [okButton] { okButton->animateClick(); });

    resize(600, 400);
}

void LogChangeDialog::setContiguousSelectionEnabled(bool enabled)
{
    if (enabled)
        m_widget->setSelectionMode(QAbstractItemView::ContiguousSelection);
    else
        m_widget->setSelectionMode(QAbstractItemView::SingleSelection);
}

bool LogChangeDialog::runDialog(const FilePath &repository,
                                const QString &commit,
                                LogChangeWidget::LogFlags flags)
{
    if (!m_widget->init(repository, commit, flags))
        return false;

    if (QDialog::exec() == QDialog::Accepted) {
        if (m_resetTypeComboBox)
            settings().lastResetIndex.setValue(m_resetTypeComboBox->currentIndex());
        return true;
    }
    return false;
}

QString LogChangeDialog::commit() const
{
    return m_widget->commit();
}

int LogChangeDialog::commitIndex() const
{
    return m_widget->commitIndex();
}

QStringList LogChangeDialog::patchRange() const
{
    return m_widget->patchRange();
}

QString LogChangeDialog::resetFlag() const
{
    if (!m_resetTypeComboBox)
        return {};
    return m_resetTypeComboBox->itemData(m_resetTypeComboBox->currentIndex()).toString();
}

LogChangeWidget *LogChangeDialog::widget() const
{
    return m_widget;
}

LogItemDelegate::LogItemDelegate(LogChangeWidget *widget) : m_widget(widget)
{
    m_widget->setItemDelegate(this);
}

int LogItemDelegate::currentRow() const
{
    return m_widget->commitIndex();
}

int LogItemDelegate::isRowSelected(int row) const
{
    return m_widget->isRowSelected(row);
}

IconItemDelegate::IconItemDelegate(LogChangeWidget *widget, const Utils::Icon &icon)
    : LogItemDelegate(widget)
    , m_icon(icon.icon())
{
}

void IconItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    QStyleOptionViewItem o = option;
    if (index.column() == 0 && hasIcon(index.row())) {
        const QSize size = option.decorationSize;
        painter->drawPixmap(o.rect.x(), o.rect.y(), m_icon.pixmap(size.width(), size.height()));
        o.rect.setLeft(size.width());
    }
    QStyledItemDelegate::paint(painter, o, index);
}

} // Git::Internal
