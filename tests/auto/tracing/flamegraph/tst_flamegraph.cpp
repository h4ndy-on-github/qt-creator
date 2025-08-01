// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <tracing/flamegraph.h>
#include <tracing/flamegraphattached.h>
#include <QObject>
#include <QStandardItemModel>
#include <QQmlComponent>
#include <QQuickItem>
#include <QTest>

class DelegateObject : public QQuickItem
{
    Q_OBJECT
};

class DelegateComponent : public QQmlComponent
{
    Q_OBJECT
public:
    QObject *create(QQmlContext *context) override;
    QObject *beginCreate(QQmlContext *) override;
    void completeCreate() override;
};

class tst_FlameGraph : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void testRebuild();
    void cleanupTestCase();

private:
    static const int sizeRole = Qt::UserRole + 1;
    static const int dataRole = Qt::UserRole + 2;
    FlameGraph::FlameGraph flameGraph;
    QStandardItemModel model;
    DelegateComponent delegate;
};

void tst_FlameGraph::initTestCase()
{
    flameGraph.setDelegate(&delegate);
    flameGraph.setModel(&model);
    flameGraph.setSizeRole(sizeRole);
    flameGraph.setWidth(100);
    flameGraph.setHeight(100);
    flameGraph.setSizeThreshold(0.01);

    QCOMPARE(flameGraph.delegate(), &delegate);
    QCOMPARE(flameGraph.model(), &model);
    QCOMPARE(flameGraph.sizeRole(), Qt::UserRole + 1);
    QCOMPARE(flameGraph.sizeThreshold(), 0.01);
}

void tst_FlameGraph::testRebuild()
{
    flameGraph.setModel(nullptr);
    qreal sum = 0;
    for (int i = 1; i < 10; ++i) {
        QStandardItem *item = new QStandardItem;
        item->setData(i, sizeRole);
        item->setData(100 / i, dataRole);

        for (int j = 1; j < i; ++j) {
            QStandardItem *item2 = new QStandardItem;
            item2->setData(1, sizeRole);
            for (int k = 0; k < 10; ++k) {
                QStandardItem *skipped = new QStandardItem;
                skipped->setData(0.001, sizeRole);
                item2->appendRow(skipped);
            }
            item->appendRow(item2);
        }

        model.appendRow(item);
        sum += i;
    }
    model.invisibleRootItem()->setData(sum, sizeRole);
    flameGraph.setModel(nullptr);
    flameGraph.setModel(&model);
    QCOMPARE(flameGraph.depth(), 3);
    qreal i = 0;
    qreal position = 0;
    const QList<QQuickItem *> children = flameGraph.childItems();
    for (QQuickItem *child : children) {
        FlameGraph::FlameGraphAttached *attached =
                FlameGraph::FlameGraph::qmlAttachedProperties(child);
        QVERIFY(attached);
        QCOMPARE(attached->relativeSize(), (++i) / sum);
        QCOMPARE(attached->relativePosition(), position / sum);
        QCOMPARE(attached->data(dataRole).toInt(), 100 / static_cast<int>(i));
        QVERIFY(attached->isDataValid());

        qreal j = 0;
        const QList<QQuickItem *> grandchildren = child->childItems();
        for (QQuickItem *grandchild : grandchildren) {
            FlameGraph::FlameGraphAttached *attached2 =
                    FlameGraph::FlameGraph::qmlAttachedProperties(grandchild);
            QVERIFY(attached2);
            QCOMPARE(attached2->relativeSize(), 1.0 / i);
            QCOMPARE(attached2->relativePosition(), (j++) / i);
            QCOMPARE(grandchild->childItems().count(), 1);
            FlameGraph::FlameGraphAttached *skipped =
                    FlameGraph::FlameGraph::qmlAttachedProperties(grandchild->childItems()[0]);
            QVERIFY(skipped);
            QCOMPARE(skipped->relativePosition(), 0.0);
            QCOMPARE(skipped->relativeSize(), 0.001 * 10);
        }

        position += i;
    }
    QCOMPARE(i, 9.0);
}

void tst_FlameGraph::cleanupTestCase()
{

}

QObject *DelegateComponent::create(QQmlContext *context)
{
    QObject *ret = beginCreate(context);
    completeCreate();
    return ret;
}

QObject *DelegateComponent::beginCreate(QQmlContext *)
{
    return new DelegateObject;
}

void DelegateComponent::completeCreate()
{
}

QTEST_MAIN(tst_FlameGraph)

#include "tst_flamegraph.moc"
