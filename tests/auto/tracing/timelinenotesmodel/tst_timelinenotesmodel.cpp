// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QColor>
#include <QSignalSpy>
#include <QTest>
#include <tracing/timelinemodelaggregator.h>
#include <tracing/timelinenotesmodel.h>

class tst_TimelineNotesModel : public QObject
{
    Q_OBJECT

private slots:
    void timelineModel();
    void addRemove();
    void properties();
    void selection();
    void modify();

private:
    Timeline::TimelineModelAggregator aggregator;
};

class TestModel : public Timeline::TimelineModel {
public:
    TestModel(Timeline::TimelineModelAggregator *parent) : TimelineModel(parent)
    {
        insert(0, 10, 10);
    }

    int typeId(int) const
    {
        return 7;
    }
};

class TestNotesModel : public Timeline::TimelineNotesModel {
    friend class tst_TimelineNotesModel;
};

void tst_TimelineNotesModel::timelineModel()
{
    TestNotesModel notes;
    TestModel *model = new TestModel(&aggregator);
    TestModel *model2 = new TestModel(&aggregator);
    notes.addTimelineModel(model);
    notes.addTimelineModel(model2);
    QCOMPARE(notes.timelineModelByModelId(model->modelId()), model);
    QCOMPARE(notes.timelineModels().count(), 2);
    QVERIFY(notes.timelineModels().contains(model));
    QVERIFY(notes.timelineModels().contains(model2));
    QVERIFY(!notes.isModified());
    notes.clear(); // clear() only clears the data, not the models
    QCOMPARE(notes.timelineModels().count(), 2);
    delete model; // notes model does monitor the destroyed() signal
    QCOMPARE(notes.timelineModels().count(), 1);
    delete model2;
}

void tst_TimelineNotesModel::addRemove()
{
    TestNotesModel notes;
    TestModel model(&aggregator);
    notes.addTimelineModel(&model);

    QSignalSpy spy(&notes, &TestNotesModel::changed);
    int id = notes.add(model.modelId(), 0, QLatin1String("xyz"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(notes.isModified(), true);
    QCOMPARE(notes.count(), 1);
    notes.resetModified();
    QCOMPARE(notes.isModified(), false);
    notes.remove(id);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(notes.isModified(), true);
    QCOMPARE(notes.count(), 0);
}

void tst_TimelineNotesModel::properties()
{

    TestNotesModel notes;
    int id = -1;
    int modelId = -1;
    {
        TestModel model(&aggregator);
        modelId = model.modelId();
        notes.addTimelineModel(&model);

        id = notes.add(model.modelId(), 0, QLatin1String("xyz"));
        QVERIFY(id >= 0);
        QCOMPARE(notes.typeId(id), 7);
        QCOMPARE(notes.timelineIndex(id), 0);
        QCOMPARE(notes.timelineModel(id), modelId);
        QCOMPARE(notes.text(id), QLatin1String("xyz"));
    }

    QCOMPARE(notes.typeId(id), -1); // cannot ask the model anymore
    QCOMPARE(notes.timelineIndex(id), 0);
    QCOMPARE(notes.timelineModel(id), modelId);
    QCOMPARE(notes.text(id), QLatin1String("xyz"));
}

void tst_TimelineNotesModel::selection()
{
    TestNotesModel notes;
    TestModel model(&aggregator);
    notes.addTimelineModel(&model);
    int id1 = notes.add(model.modelId(), 0, QLatin1String("blablub"));
    int id2 = notes.add(model.modelId(), 0, QLatin1String("xyz"));
    QVariantList ids = notes.byTimelineModel(model.modelId());
    QCOMPARE(ids.length(), 2);
    QVERIFY(ids.contains(id1));
    QVERIFY(ids.contains(id2));

    ids = notes.byTypeId(7);
    QCOMPARE(ids.length(), 2);
    QVERIFY(ids.contains(id1));
    QVERIFY(ids.contains(id2));

    int got = notes.get(model.modelId(), 0);
    QVERIFY(got == id1 || got == id2);
    QCOMPARE(notes.get(model.modelId(), 20), -1);
    QCOMPARE(notes.get(model.modelId() + 10, 10), -1);
}

void tst_TimelineNotesModel::modify()
{
    TestNotesModel notes;
    TestModel model(&aggregator);
    notes.addTimelineModel(&model);
    QSignalSpy spy(&notes, &TestNotesModel::changed);
    int id = notes.add(model.modelId(), 0, QLatin1String("a"));
    QCOMPARE(spy.count(), 1);
    notes.resetModified();
    notes.update(id, QLatin1String("b"));
    QVERIFY(notes.isModified());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(notes.text(id), QLatin1String("b"));
    notes.resetModified();
    notes.update(id, QLatin1String("b"));
    QVERIFY(!notes.isModified());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(notes.text(id), QLatin1String("b"));

    notes.setText(id, QLatin1String("a"));
    QVERIFY(notes.isModified());
    QCOMPARE(spy.count(), 3);
    QCOMPARE(notes.text(id), QLatin1String("a"));
    notes.resetModified();

    notes.setText(model.modelId(), 0, QLatin1String("x"));
    QVERIFY(notes.isModified());
    QCOMPARE(spy.count(), 4);
    QCOMPARE(notes.text(id), QLatin1String("x"));
    notes.resetModified();

    TestModel model2(&aggregator);
    notes.addTimelineModel(&model2);
    notes.setText(model2.modelId(), 0, QLatin1String("hh"));
    QVERIFY(notes.isModified());
    QCOMPARE(spy.count(), 5);
    QCOMPARE(notes.count(), 2);
    notes.resetModified();

    notes.setText(id, QString());
    QVERIFY(notes.isModified());
    QCOMPARE(spy.count(), 6);
    QCOMPARE(notes.count(), 1);
    notes.resetModified();

}

QTEST_GUILESS_MAIN(tst_TimelineNotesModel)

#include "tst_timelinenotesmodel.moc"
