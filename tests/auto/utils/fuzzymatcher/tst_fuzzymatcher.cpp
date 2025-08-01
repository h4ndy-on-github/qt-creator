// Copyright (C) 2017 BlackBerry Limited <qt@blackberry.com>
// Copyright (C) 2017 Andre Hartmann <aha_1980@gmx.de>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <utils/fuzzymatcher.h>

#include <QTest>

class tst_FuzzyMatcher : public QObject
{
    Q_OBJECT

private slots:
    void fuzzyMatcher();
    void fuzzyMatcher_data();
    void fuzzyMatcherMultiWord();
    void fuzzyMatcherMultiWord_data();
    void highlighting();
    void highlighting_data();
    void highlightingMultiWord();
    void highlightingMultiWord_data();
};

void tst_FuzzyMatcher::fuzzyMatcher()
{
    QFETCH(QString, pattern);
    QFETCH(QString, candidate);
    QFETCH(int, expectedIndex);

    const QRegularExpression regExp = FuzzyMatcher::createRegExp(pattern);
    const QRegularExpressionMatch match = regExp.match(candidate);
    QCOMPARE(match.capturedStart(), expectedIndex);
}

void tst_FuzzyMatcher::fuzzyMatcher_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<int>("expectedIndex");

    QTest::newRow("underscore") << "vl" << "very_long_camel_hump" << 0;
    QTest::newRow("underscore-uppercase") << "vl" << "VERY_LONG_CAMEL_HUMP" << 0;
    QTest::newRow("exact") << "VeryLongCamelHump" << "VeryLongCamelHump" << 0;
    QTest::newRow("prefix-segments") << "velo" << "very_long_Camel_hump" << 0;
    QTest::newRow("humps") << "vlc" << "VeryLongCamelHump" << 0;
    QTest::newRow("case-insensitive-humps") << "VlCh" << "VeryLongCamelHump" << 0;
    QTest::newRow("incorrect-hump") << "vxc" << "VeryLongCamelHump" << -1;
    QTest::newRow("skipped-hump") << "vc" << "VeryLongCamelHump" << -1;
    QTest::newRow("middle-humps") << "lc" << "VeryLongCamelHump" << 4;
    QTest::newRow("incorrect-hump") << "lyn" << "VeryLongCamelHump" << -1;
    QTest::newRow("humps") << "VL" << "VeryLongCamelHump" << 0;
    QTest::newRow("skipped-humps-upper") << "VH" << "VeryLongCamelHump" << -1;
    QTest::newRow("numbers-searched") << "4" << "Test4Fun" << 4;
    QTest::newRow("numbers-skipped") << "fpt" << "Fancy4PartThingy" << 0;
    QTest::newRow("numbers-camel") << "f4pt" << "Fancy4PartThingy" << 0;
    QTest::newRow("multi-numbers-camel") << "f4pt" << "Fancy4567PartThingy" << 0;
    QTest::newRow("numbers-underscore") << "f4pt" << "fancy_4_part_thingy" << 0;
    QTest::newRow("numbers-underscore-upper") << "f4pt" << "FANCY_4_PART_THINGY" << 0;
    QTest::newRow("question-wildcard") << "Lon?Ca" << "VeryLongCamelHump" << 4;
    QTest::newRow("unmatched-question-wildcard") << "Long?Ca" << "VeryLongCamelHump" << -1;
    QTest::newRow("asterisk-wildcard") << "Long*Ca" << "VeryLongCamelHump" << 4;
    QTest::newRow("empty-asterisk-wildcard") << "Lo*Ca" << "VeryLongCamelHump" << 4;
    QTest::newRow("no-partial") << "NCH" << "LongCamelHump" << -1;
    QTest::newRow("middle-after-number") << "CH" << "Long1CamelHump" << 5;
    QTest::newRow("middle-after-underscore") << "CH" << "long_camel_hump" << 5;
    QTest::newRow("middle-after-underscore-uppercase") << "CH" << "LONG_CAMEL_HUMP" << 5;
    QTest::newRow("middle-continued") << "cahu" << "LongCamelHump" << 4;
    QTest::newRow("middle-no-hump") << "window" << "mainwindow.cpp" << 4;
    QTest::newRow("case-insensitive") << "window" << "MAINWINDOW.cpp" << 4;
    QTest::newRow("case-insensitive-2") << "wINDow" << "MainwiNdow.cpp" << 4;
}

void tst_FuzzyMatcher::fuzzyMatcherMultiWord()
{
    QFETCH(QString, pattern);
    QFETCH(QString, candidate);
    QFETCH(int, expectedIndex);

    const QRegularExpression regExp
        = FuzzyMatcher::createRegExp(pattern, FuzzyMatcher::CaseSensitivity::CaseInsensitive, true);
    const QRegularExpressionMatch match = regExp.match(candidate);
    QCOMPARE(match.capturedStart(), expectedIndex);
}

void tst_FuzzyMatcher::fuzzyMatcherMultiWord_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<int>("expectedIndex");

    QTest::newRow("one_word") << "fo" << "foo" << 0;
    QTest::newRow("one_word_complete") << "foo" << "foo" << 0;
    QTest::newRow("one_word_mismatch") << "bar" << "foo" << -1;
    QTest::newRow("two_words") << "fb" << "foo bar" << 0;
    QTest::newRow("two_wordslU") << "fb" << "Foo Bar" << 0;
    QTest::newRow("two_wordsUl") << "FB" << "foo bar" << 0;
    QTest::newRow("two_words_one_match") << "ba" << "foo bar" << 4;
    QTest::newRow("two_words_complete_match") << "foo bar" << "foo bar" << 0;
    QTest::newRow("wrong_order") << "bf" << "foo bar" << -1;
    QTest::newRow("no_space") << "fb" << "foobar" << -1;
    QTest::newRow("inword_first_match") << "oob" << "foo bar" << -1;
    QTest::newRow("inword_second_match") << "foar" << "foo bar" << -1;
}

typedef QList<QPair<int, int>> Matches;

void tst_FuzzyMatcher::highlighting()
{
    QFETCH(QString, pattern);
    QFETCH(QString, candidate);
    QFETCH(Matches, matches);

    const QRegularExpression regExp = FuzzyMatcher::createRegExp(pattern);
    const QRegularExpressionMatch match = regExp.match(candidate);
    const FuzzyMatcher::HighlightingPositions positions =
            FuzzyMatcher::highlightingPositions(match);

    QCOMPARE(positions.starts.size(), matches.size());
    for (int i = 0; i < positions.starts.size(); ++i) {
        const QPair<int, int> &match = matches.at(i);
        QCOMPARE(positions.starts.at(i), match.first);
        QCOMPARE(positions.lengths.at(i), match.second);
    }
}

void tst_FuzzyMatcher::highlighting_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<Matches>("matches");

    QTest::newRow("prefix-snake") << "very" << "very_long_camel_hump"
                                  << Matches{{0, 4}};
    QTest::newRow("middle-snake") << "long" << "very_long_camel_hump"
                                  << Matches{{5, 4}};
    QTest::newRow("suffix-snake") << "hump" << "very_long_camel_hump"
                                  << Matches{{16, 4}};
    QTest::newRow("prefix-camel") << "very" << "VeryLongCamelHump"
                                  << Matches{{0, 4}};
    QTest::newRow("middle-camel") << "Long" << "VeryLongCamelHump"
                                  << Matches{{4, 4}};
    QTest::newRow("suffix-camel") << "Hump" << "VeryLongCamelHump"
                                  << Matches{{13, 4}};
    QTest::newRow("humps-camel")  << "vlch" << "VeryLongCamelHump"
                                  << Matches{{0, 1}, {4, 1}, {8, 1}, {13, 1}};
    QTest::newRow("humps-camel-lower") << "vlch" << "veryLongCamelHump"
                                       << Matches{{0, 1}, {4, 1}, {8, 1}, {13, 1}};
    QTest::newRow("humps-snake") << "vlch" << "very_long_camel_hump"
                                 << Matches{{0, 1}, {5, 1}, {10, 1}, {16, 1}};
    QTest::newRow("humps-middle") << "lc" << "VeryLongCamelHump"
                                  << Matches{{4, 1}, {8, 1}};
    QTest::newRow("humps-last") << "h" << "VeryLongCamelHump"
                                << Matches{{13, 1}};
    QTest::newRow("humps-continued") << "LoCa" << "VeryLongCamelHump"
                                     << Matches{{4, 2}, {8, 2}};
    QTest::newRow("duplicate-match") << "som" << "SomeMatch"
                                     << Matches{{0, 3}};
    QTest::newRow("numbers-searched") << "4" << "TestJust4Fun"
                                      << Matches{{8, 1}};
    QTest::newRow("numbers-plain") << "boot2qt" << "use_boot2qt_for_embedded"
                                   << Matches{{4, 7}};
    QTest::newRow("numbers-skipped") << "fpt" << "Fancy4PartThingy"
                                     << Matches{{0, 1}, {6, 1}, {10, 1}};
    QTest::newRow("numbers-camel") << "f4pt" << "Fancy4PartThingy"
                                   << Matches{{0, 1}, {5, 2}, {10, 1}};
    QTest::newRow("numbers-snake") << "f4pt" << "fancy_4_part_thingy"
                                   << Matches{{0, 1}, {6, 1}, {8, 1}, {13, 1}};
    QTest::newRow("numbers-snake-upper") << "f4pt" << "FANCY_4_PART_THINGY"
                                         << Matches{{0, 1}, {6, 1}, {8, 1}, {13, 1}};
    QTest::newRow("wildcard-asterisk") << "Lo*Hu" << "VeryLongCamelHump"
                                       << Matches{{4, 2}, {13, 2}};
    QTest::newRow("wildcard-question") << "Lo?g" << "VeryLongCamelHump"
                                       << Matches{{4, 2}, {7, 1}};
    QTest::newRow("middle-no-hump") << "window" << "mainwindow.cpp"
                                    << Matches{{4, 6}};
    QTest::newRow("uppercase-word-and-humps") << "htvideoele" << "HTMLVideoElement"
                                              << Matches{{0, 2}, {4, 8}};
}

void tst_FuzzyMatcher::highlightingMultiWord()
{
    QFETCH(QString, pattern);
    QFETCH(QString, candidate);
    QFETCH(Matches, matches);

    const QRegularExpression regExp
        = FuzzyMatcher::createRegExp(pattern, FuzzyMatcher::CaseSensitivity::CaseInsensitive, true);
    const QRegularExpressionMatch match = regExp.match(candidate);
    const FuzzyMatcher::HighlightingPositions positions
        = FuzzyMatcher::highlightingPositions(match);

    QCOMPARE(positions.starts.size(), matches.size());
    for (int i = 0; i < positions.starts.size(); ++i) {
        const QPair<int, int> &match = matches.at(i);
        QCOMPARE(positions.starts.at(i), match.first);
        QCOMPARE(positions.lengths.at(i), match.second);
    }
}

void tst_FuzzyMatcher::highlightingMultiWord_data()
{
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<Matches>("matches");

    QTest::newRow("one_word") << "fo"
                              << "foo" << Matches{{0, 2}};
    QTest::newRow("one_word_complete") << "foo"
                                       << "foo" << Matches{{0, 3}};
    QTest::newRow("one_word_mismatch") << "bar"
                                       << "foo" << Matches{};
    QTest::newRow("two_words") << "fb"
                               << "foo bar" << Matches{{0, 1}, {4, 1}};
    QTest::newRow("two_wordslU") << "fb"
                                 << "Foo Bar" << Matches{{0, 1}, {4, 1}};
    QTest::newRow("two_wordsUl") << "FB"
                                 << "foo bar" << Matches{{0, 1}, {4, 1}};
    QTest::newRow("two_words_one_match") << "ba"
                                         << "foo bar" << Matches{{4, 2}};
    QTest::newRow("two_words_complete_match") << "foo bar"
                                              << "foo bar" << Matches{{0, 7}};
    QTest::newRow("wrong_order") << "bf"
                                 << "foo bar" << Matches{};
    QTest::newRow("no_space") << "fb"
                              << "foobar" << Matches{};
    QTest::newRow("inword_first_match") << "oob"
                                        << "foo bar" << Matches{};
    QTest::newRow("inword_second_match") << "foar"
                                         << "foo bar" << Matches{};
}

QTEST_APPLESS_MAIN(tst_FuzzyMatcher)
#include "tst_fuzzymatcher.moc"
