#include "editor/brackets.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

class BracketBenchmarks : public QObject {
    Q_OBJECT

private slots:
    void matching_data() {
        QTest::addColumn<bool>("hasPartner");
        QTest::addColumn<bool>("backward");
        QTest::addColumn<bool>("awayFromBracket");
        QTest::newRow("distant-forward") << true << false << false;
        QTest::newRow("distant-backward") << true << true << false;
        QTest::newRow("unmatched") << false << false << false;
        QTest::newRow("away-from-bracket") << true << false << true;
    }

    void matching() {
        QFETCH(bool, hasPartner);
        QFETCH(bool, backward);
        QFETCH(bool, awayFromBracket);

        QTextDocument document;
        const QString text = "(\n" + QString("ordinary text without brackets\n").repeated(100000)
                             + (hasPartner ? ")" : "");
        document.setPlainText(text);
        BracketHighlighter highlighter(&document);
        QTextCursor cursor(&document);
        cursor.setPosition(awayFromBracket ? 4 : backward ? text.size() : 0);

        BracketMatch match;
        QBENCHMARK {
            match = findBracketMatch(cursor);
        }
        const int expectedPosition = backward ? text.size() - 1 : 0;
        const int expectedPartner = backward ? 0 : text.size() - 1;
        QCOMPARE(match.position, awayFromBracket ? -1 : expectedPosition);
        QCOMPARE(match.partner, awayFromBracket || !hasPartner ? -1 : expectedPartner);
    }
};

QTEST_MAIN(BracketBenchmarks)
#include "bracketbenchmarks.moc"
