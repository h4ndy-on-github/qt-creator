// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <qmljs/qmljssimplereader.h>

#include <QTest>
#include <algorithm>

using namespace QmlJS;

class tst_SimpleReader : public QObject
{
    Q_OBJECT
public:
    tst_SimpleReader();

private slots:
    void testWellFormed();
    void testIllFormed01();
    void testIllFormed02();
    void testArrays();
    void testBug01();
};

tst_SimpleReader::tst_SimpleReader()
{
}

void tst_SimpleReader::testWellFormed()
{
    char source[] = "RootNode {\n"
                    "   ChildNode {\n"
                    "       property01: 10\n"
                    "   }\n"
                    "   ChildNode {\n"
                    "       propertyString: \"str\"\n"
                    "       InnerChild {\n"
                    "           test: \"test\"\n"
                    "       }\n"
                    "   }\n"
                    "   propertyBlah: false\n"
                    "}\n";

    SimpleReaderNode::WeakPtr weak01;
    SimpleReaderNode::WeakPtr weak02;
    {
        SimpleReader reader;
        SimpleReaderNode::Ptr rootNode = reader.readFromSource(QString::fromUtf8(source));
        QVERIFY(reader.errors().isEmpty());
        QVERIFY(rootNode);
        QVERIFY(rootNode->isValid());
        QCOMPARE(rootNode->name(), QLatin1String("RootNode"));

        QCOMPARE(rootNode->children().count(), 2);
        QCOMPARE(rootNode->properties().count(), 1);

        QVERIFY(rootNode->properties().contains("propertyBlah"));
        QCOMPARE(rootNode->property("property01").value.toBool(), false);

        QVERIFY(rootNode->children().first()->isValid());
        QVERIFY(!rootNode->children().first()->isRoot());

        QVERIFY(rootNode->children().first()->properties().contains("property01"));
        QCOMPARE(rootNode->children().first()->property("property01").value.toInt(), 10);

        SimpleReaderNode::Ptr secondChild = rootNode->children().at(1);

        QVERIFY(secondChild);
        QVERIFY(secondChild->isValid());
        QVERIFY(!secondChild->isRoot());
        QCOMPARE(secondChild->name(), QLatin1String("ChildNode"));

        QVERIFY(secondChild->properties().contains("propertyString"));
        QCOMPARE(secondChild->property("propertyString").value.toString(), QLatin1String("str"));

        QCOMPARE(secondChild->children().count(), 1);

        SimpleReaderNode::Ptr innerChild = secondChild->children().first();

        QVERIFY(innerChild);
        QVERIFY(innerChild->isValid());
        QVERIFY(!innerChild->isRoot());
        QCOMPARE(innerChild->name(), QLatin1String("InnerChild"));

        QVERIFY(innerChild->properties().contains("test"));
        QCOMPARE(innerChild->property("test").value.toString(), QLatin1String("test"));

        weak01 = rootNode;
        weak02 = secondChild;
    }

    QVERIFY(!weak01);
    QVERIFY(!weak02);
}

void tst_SimpleReader::testIllFormed01()
{
    char source[] = "RootNode {\n"
                    "   ChildNode {\n"
                    "       property01: 10\n"
                    "   }\n"
                    "   ChildNode {\n"
                    "       propertyString: \"str\"\n"
                    "       InnerChild \n" //missing {
                    "           test: \"test\"\n"
                    "       }\n"
                    "   }\n"
                    "   propertyBlah: false\n"
                    "}\n";
    SimpleReader reader;
    SimpleReaderNode::Ptr rootNode = reader.readFromSource(QString::fromUtf8(source));

    QVERIFY(!rootNode);
    QVERIFY(!reader.errors().empty());
}

void tst_SimpleReader::testIllFormed02()
{
        char source[] = "RootNode {\n"
                    "   ChildNode {\n"
                    "       property01: 10\n"
                    "       property01: 20\n"
                    "   }\n"
                    "   ChildNode {\n"
                    "       propertyString: \"str\"\n"
                    "       InnerChild {\n"
                    "           test: \"test\"\n"
                    "           test: \"test2\"\n"
                    "       }\n"
                    "   }\n"
                    "}\n";

        SimpleReader reader;
        SimpleReaderNode::Ptr rootNode = reader.readFromSource(QString::fromUtf8(source));

        QVERIFY(rootNode);
        QVERIFY(rootNode->isValid());
        QVERIFY(rootNode->isRoot());

        QVERIFY(!reader.errors().empty());
        QCOMPARE(reader.errors().count(), 2);

        SimpleReaderNode::Ptr firstChild = rootNode->children().at(0);

        QVERIFY(firstChild);
        QVERIFY(firstChild->isValid());
        QVERIFY(!firstChild->isRoot());

        QCOMPARE(firstChild->properties().count(), 1);
        QVERIFY(firstChild->properties().contains("property01"));
        QCOMPARE(firstChild->property("property01").value.toString(), QLatin1String("20"));
}

void tst_SimpleReader::testArrays()
{
    char source[] = "RootNode {\n"
                    "   propertyArray: [\"string01\", \"string02\" ]\n"
                    "   ChildNode {\n"
                    "       propertyArray: [\"string01\", \"string02\" ]\n"
                    "       propertyArrayMixed: [\"string03\", [\"string01\", \"string02\"] ]\n"
                    "   }\n"
                    "}\n";

        QList<QVariant> variantList;
        variantList << QVariant(QLatin1String("string01")) << QVariant(QLatin1String("string02"));
        const QVariant variant = variantList;

        SimpleReader reader;
        SimpleReaderNode::Ptr rootNode = reader.readFromSource(QString::fromUtf8(source));

        QVERIFY(rootNode);
        QVERIFY(rootNode->isValid());
        QVERIFY(rootNode->isRoot());

        QCOMPARE(rootNode->property("propertyArray").value, variant);


        SimpleReaderNode::Ptr firstChild = rootNode->children().at(0);

        QVERIFY(firstChild);
        QVERIFY(firstChild->isValid());
        QVERIFY(!firstChild->isRoot());
        QCOMPARE(firstChild->property("propertyArray").value, variant);

        QList<QVariant> variantList2;
        variantList2 << QVariant(QLatin1String("string03")) << variant;
        const QVariant variant2 = variantList2;

        QCOMPARE(firstChild->property("propertyArrayMixed").value, variant2);
}

void tst_SimpleReader::testBug01()
{
    char source[] = "\n"
        "AutoTypes {\n"
        "    imports: [ \"import HelperWidgets 1.0\", \"import QtQuick 1.0\", \"import Bauhaus 1.0\" ]\n"
        "    Type {\n"
        "        typeNames: [\"int\"]\n"
        "        sourceFile: \"IntEditorTemplate.qml\"\n"
        "    }\n"
        "    Type {\n"
        "        typeNames: [\"real\", \"double\", \"qreal\"]\n"
        "        sourceFile: \"RealEditorTemplate.qml\"\n"
        "    }\n"
        "    Type {\n"
        "        typeNames: [\"string\", \"QString\", \"QUrl\", \"url\"]\n"
        "        sourceFile: \"StringEditorTemplate.qml\"\n"
        "    }\n"
        "    Type {\n"
        "        typeNames: [\"bool\", \"boolean\"]\n"
        "        sourceFile: \"BooleanEditorTemplate.qml\"\n"
        "    }\n"
        "    Type {\n"
        "        typeNames: [\"color\", \"QColor\"]\n"
        "        sourceFile: \"ColorEditorTemplate.qml\"\n"
        "    }\n"
        "}\n";

    SimpleReader reader;
    SimpleReaderNode::Ptr rootNode = reader.readFromSource(QString::fromUtf8(source));

    QVERIFY(rootNode);
    QVERIFY(rootNode->isValid());
    QVERIFY(rootNode->isRoot());

    QCOMPARE(rootNode->propertyNames().count(), 1);
}

QTEST_GUILESS_MAIN(tst_SimpleReader);

#include "tst_qmljssimplereader.moc"
