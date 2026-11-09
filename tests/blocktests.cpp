#include "blocktests.h"
#include "editor/blocks.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

void BlockTests::locating_data()
{
    QTest::addColumn<QString>("beforeMarker");
    QTest::addColumn<QString>("markerLine");
    QTest::addColumn<QString>("afterMarker");
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("evaluator");
    QTest::addColumn<QString>("replaced");

    QTest::newRow("first") << ">>\n2+3\n" << "==" << "" << "2+3" << "" << "";
    QTest::newRow("empty-input") << ">>\n" << "==" << "" << "" << "" << "";
    QTest::newRow("empty-lines") << ">>\n\n2+3\n\n" << "==" << "" << "\n2+3\n" << "" << "";
    QTest::newRow("only-empty-lines") << ">>\n\n\n" << "==" << "" << "\n" << "" << "";
    QTest::newRow("explicit-evaluator") << ">>\n2+3\n"
                                        << "== \tarith  " << "" << "2+3" << "arith" << "";
    QTest::newRow("nearest-start") << ">>\nignored\n>>\n2+3\n"
                                   << "==" << "" << "2+3" << "" << "";
    QTest::newRow("exact-start") << ">>\n >>\n>> \n" << "==" << "" << " >>\n>> " << "" << "";
    QTest::newRow("replace-to-document-end")
        << ">>\n2+3\n"
        << "==" << "\nold\n##" << "2+3" << "" << "\nold\n##";
    QTest::newRow("first-end") << ">>\n2+3\n"
                               << "==" << "\nold\n##\ntail\n##" << "2+3" << "" << "\nold\n##";
    QTest::newRow("exact-end") << ">>\n2+3\n"
                               << "==" << "\n ##\n## \n##\n"
                               << "2+3" << "" << "\n ##\n## \n##";
    QTest::newRow("missing-end") << ">>\n2+3\n" << "==" << "\nkeep\n" << "2+3" << "" << "";
    QTest::newRow("next-start-boundary") << ">>\n2+3\n"
                                         << "==" << "\nold\n>>\n7\n##" << "2+3" << "" << "";
    QTest::newRow("adjacent-start-boundary") << ">>\n2+3\n"
                                             << "==" << "\n>>\n##" << "2+3" << "" << "";
    QTest::newRow("evaluation-line-in-output")
        << ">>\n2+3\n"
        << "==" << "\n==other\n##" << "2+3" << "" << "\n==other\n##";
    QTest::newRow("unicode-offsets")
        << QString::fromUtf8("😀\n>>\nä😀\n") << "==" << QString::fromUtf8("\n😀\n##\n尾")
        << QString::fromUtf8("ä😀") << "" << QString::fromUtf8("\n😀\n##");
    QTest::newRow("plain-text-normalization") << ">>\nx\u00a0=\u00a03\u2028x+1\n"
                                              << "==\u00a0arith\u00a0" << ""
                                              << "x = 3\nx+1" << "arith" << "";
    QTest::newRow("soft-start") << "ignored\u2028>>\u2028one\u2028two\n"
                                << "==" << "" << "one\ntwo" << "" << "";
    QTest::newRow("soft-end") << ">>\n2+3\n"
                              << "==arith" << "\u2028old\u2028##\u2028keep"
                              << "2+3" << "arith" << "\u2028old\u2028##";
    QTest::newRow("soft-next-start")
        << ">>\n2+3\n"
        << "==" << "\nold\u2028>>\u2028next\u2028##" << "2+3" << "" << "";
    QTest::newRow("paragraph-separators")
        << ">>\u20292+3\u2029" << "==" << "\u2029old\u2029##" << "2+3" << ""
        << "\u2029old\u2029##";
}

void BlockTests::locating()
{
    QFETCH(QString, beforeMarker);
    QFETCH(QString, markerLine);
    QFETCH(QString, afterMarker);
    QFETCH(QString, input);
    QFETCH(QString, evaluator);
    QFETCH(QString, replaced);

    QTextDocument document;
    document.setPlainText(beforeMarker + markerLine + afterMarker);
    document.setModified(false);
    const auto result = locateBlock(document.findBlock(beforeMarker.size()));
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.input, input);
    QCOMPARE(result.evaluator, evaluator);
    QCOMPARE(result.insertion, beforeMarker.size() + markerLine.size());
    QCOMPARE(result.replacementEnd, result.insertion + replaced.size());
    QVERIFY(!document.isModified());
    QVERIFY(!document.isUndoAvailable());
}

void BlockTests::invalidMarkers_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("error");
    QTest::newRow("empty-document") << "" << "Invalid evaluation marker";
    QTest::newRow("indented-marker") << ">>\n1\n ==" << "Invalid evaluation marker";
    QTest::newRow("incomplete-marker") << ">>\n1\n=" << "Invalid evaluation marker";
    QTest::newRow("marker-at-start") << "==" << "Missing >> marker";
    QTest::newRow("missing-start") << "1\n==" << "Missing >> marker";
    QTest::newRow("indented-start") << " >>\n1\n==" << "Missing >> marker";
    QTest::newRow("start-with-suffix") << ">> \n1\n==" << "Missing >> marker";
    QTest::newRow("start-with-nbsp") << ">>\u00a0\n1\n==" << "Missing >> marker";
}

void BlockTests::invalidMarkers()
{
    QFETCH(QString, text);
    QFETCH(QString, error);
    QTextDocument document(text);
    const auto result = locateBlock(document.lastBlock());
    QVERIFY(!result.valid);
    QCOMPARE(result.error, error);
}

void BlockTests::invalidBlock()
{
    const auto result = locateBlock(QTextBlock());
    QVERIFY(!result.valid);
    QCOMPARE(result.error, QString("Invalid evaluation marker"));
}

void BlockTests::editsAndUndoRedo()
{
    QTextDocument document(">>\n1\n==\nold\n##");
    QTextCursor cursor(&document);
    cursor.setPosition(3);
    cursor.setPosition(4, QTextCursor::KeepAnchor);
    cursor.insertText(QString::fromUtf8("ä😀\n2"));

    auto result = locateBlock(document.find("==").block());
    QVERIFY(result.valid);
    QCOMPARE(result.input, QString::fromUtf8("ä😀\n2"));
    QCOMPARE(result.insertion, 11);
    QCOMPARE(result.replacementEnd, 18);

    document.undo();
    result = locateBlock(document.find("==").block());
    QVERIFY(result.valid);
    QCOMPARE(result.input, QString("1"));
    QCOMPARE(result.insertion, 7);
    QCOMPARE(result.replacementEnd, 14);

    document.redo();
    result = locateBlock(document.find("==").block());
    QVERIFY(result.valid);
    QCOMPARE(result.input, QString::fromUtf8("ä😀\n2"));
    QCOMPARE(result.insertion, 11);
    QCOMPARE(result.replacementEnd, 18);
}

void BlockTests::largeSurroundingDocument()
{
    const QString padding = QString("unrelated text\n").repeated(50000);
    const QString beforeMarker = padding + ">>\n2+3\n";
    QTextDocument document(beforeMarker + "==arith\nold\n##\n" + padding);
    const auto result = locateBlock(document.findBlock(beforeMarker.size()));
    QVERIFY(result.valid);
    QCOMPARE(result.input, QString("2+3"));
    QCOMPARE(result.evaluator, QString("arith"));
    QCOMPARE(result.insertion, beforeMarker.size() + 7);
    QCOMPARE(result.replacementEnd, beforeMarker.size() + 14);
}

QTEST_MAIN(BlockTests)
