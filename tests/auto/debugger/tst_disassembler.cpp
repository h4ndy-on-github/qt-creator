// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "disassemblerlines.h"

#include <QTest>

//TESTED_COMPONENT=src/plugins/debugger/gdb

Q_DECLARE_METATYPE(Debugger::Internal::DisassemblerLine)

using namespace Debugger::Internal;

class tst_disassembler : public QObject
{
    Q_OBJECT

public:
    tst_disassembler() {}

private slots:
    void parse();
    void parse_data();

private:
    DisassemblerLines lines;
};

void tst_disassembler::parse()
{
    QFETCH(QString, raw);
    QFETCH(QString, cooked);
    QFETCH(int, bytes);
    QFETCH(Debugger::Internal::DisassemblerLine, line);

    lines.appendUnparsed(raw);
    DisassemblerLine parsed = lines.at(lines.size() - 1);

    QCOMPARE(parsed.address, line.address);
    QCOMPARE(parsed.function, line.function);
    QCOMPARE(parsed.offset, line.offset);
    QCOMPARE(parsed.lineNumber, line.lineNumber);
    QCOMPARE(parsed.rawData, line.rawData);
    QCOMPARE(parsed.data, line.data);

    QString out___ = parsed.toString(bytes);
    QCOMPARE(out___, cooked);
}

void tst_disassembler::parse_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("cooked");
    QTest::addColumn<int>("bytes");
    QTest::addColumn<Debugger::Internal::DisassemblerLine>("line");

    DisassemblerLine line;

    line.address = 0x40f39e;
    line.offset  = 18;
    line.data    = "mov    %rax,%rdi";
    QTest::newRow("plain")
           << "0x000000000040f39e <+18>:\tmov    %rax,%rdi"
           << "0x40f39e  <+   18>         mov    %rax,%rdi"
           << 0 << line;

    line.address = 0x40f3a1;
    line.offset  = 21;
    line.data    = "callq  0x420d2c <_ZN7qobject5Names3Bar10TestObjectC2EPN4Myns7QObjectE>";
    QTest::newRow("call")
           << "0x000000000040f3a1 <+21>:\tcallq  "
                "0x420d2c <_ZN7qobject5Names3Bar10TestObjectC2EPN4Myns7QObjectE>"
           << "0x40f3a1  <+   21>         callq  "
                "0x420d2c <_ZN7qobject5Names3Bar10TestObjectC2EPN4Myns7QObjectE>"
           << 0 << line;


    line.address = 0x000000000041cd73;
    line.offset  = 0;
    line.data    = "mov    %rax,%rdi";
    QTest::newRow("set print max-symbolic-offset 1, plain")
            << "0x000000000041cd73:\tmov    %rax,%rdi"
            << "0x41cd73                   mov    %rax,%rdi"
            << 0 << line;

    line.address = 0x000000000041cd73;
    line.offset  = 0;
    line.data    = "callq  0x420d2c <_ZN4Myns12QApplicationC1ERiPPci@plt>";
    QTest::newRow("set print max-symbolic-offset 1, call")
            << "0x00000000041cd73:\tcallq  0x420d2c <_ZN4Myns12QApplicationC1ERiPPci@plt>"
            << "0x41cd73                   callq  0x420d2c <_ZN4Myns12QApplicationC1ERiPPci@plt>"
            << 0 << line;

    // With raw bytes:
    line.address  = 0x00000000004010d3;
    line.offset   = 0;
    line.function = "main()";
    line.offset   = 132;
    line.bytes    = "48 89 c7";
    line.data     = "mov    %rax,%rdi";
    QTest::newRow("with raw bytes")
            << "   0x00000000004010d3 <main()+132>:\t48 89 c7\tmov    %rax,%rdi"
            << "0x4010d3  <+  132>        48 89 c7   mov    %rax,%rdi"
            << 10 << line;
 }


QTEST_APPLESS_MAIN(tst_disassembler);

#include "tst_disassembler.moc"

