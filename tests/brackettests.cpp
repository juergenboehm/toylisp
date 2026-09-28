#include "editor/brackets.h"
#include "ui/editor.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

class CountingHighlighter : public BracketHighlighter {
public:
    using BracketHighlighter::BracketHighlighter;
    QVector<int> highlightedBlocks;

protected:
    void highlightBlock(const QString &text) override {
        highlightedBlocks.append(currentBlock().blockNumber());
        BracketHighlighter::highlightBlock(text);
    }
};

class BracketTests : public QObject {
    Q_OBJECT

    static void moveCursor(Editor &editor, int position) {
        auto cursor = editor.textCursor();
        cursor.setPosition(position);
        editor.setTextCursor(cursor);
    }

    static QVector<int> highlightedPositions(const Editor &editor) {
        QVector<int> positions;
        for (const auto &selection : editor.extraSelections())
            positions.append(selection.cursor.selectionStart());
        return positions;
    }

private slots:
    void matching_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("cursorPosition");
        QTest::addColumn<int>("bracketPosition");
        QTest::addColumn<int>("partnerPosition");

        QTest::newRow("empty") << "" << 0 << -1 << -1;
        QTest::newRow("plain") << "abc" << 1 << -1 << -1;
        QTest::newRow("away-from-brackets") << "( abc )" << 3 << -1 << -1;
        QTest::newRow("opening") << "([]{})" << 0 << 0 << 5;
        QTest::newRow("closing") << "([]{})" << 5 << 5 << 0;
        QTest::newRow("square") << "([]{})" << 1 << 1 << 2;
        QTest::newRow("brace-forward") << "([]{})" << 3 << 3 << 4;
        QTest::newRow("brace-backward") << "([]{})" << 4 << 4 << 3;
        QTest::newRow("after-opening") << "(a)" << 1 << 0 << 2;
        QTest::newRow("document-end") << "(a)" << 3 << 2 << 0;
        QTest::newRow("prefer-right") << "()[]" << 2 << 2 << 3;
        QTest::newRow("block-end") << "(\n[1]\n)" << 1 << 0 << 6;
        QTest::newRow("block-start") << "(\n[1]\n)" << 2 << 2 << 4;
        QTest::newRow("multiline-forward") << "(\n[1]\n)" << 0 << 0 << 6;
        QTest::newRow("multiline-backward") << "(\n[1]\n)" << 6 << 6 << 0;
        QTest::newRow("empty-blocks") << "(\n\ntext\n\n)" << 10 << 9 << 0;
        QTest::newRow("after-newline") << "(\na\n)" << 2 << -1 << -1;
        QTest::newRow("crossed-forward") << "([)]" << 0 << 0 << -1;
        QTest::newRow("crossed-backward") << "([)]" << 3 << 3 << -1;
        QTest::newRow("mismatch-across-blocks") << "(\n[\n)\n]" << 0 << 0 << -1;
        QTest::newRow("unclosed") << "(" << 0 << 0 << -1;
        QTest::newRow("unopened") << ")" << 1 << 0 << -1;
        QTest::newRow("inner-pair") << "([]" << 1 << 1 << 2;
        QTest::newRow("textual-string") << "\"(\" x )" << 1 << 1 << 6;
        QTest::newRow("textual-comment") << "; (\n)" << 2 << 2 << 4;
        QTest::newRow("unicode-offsets") << QString::fromUtf8("😀(ä)") << 2 << 2 << 4;
        QTest::newRow("combining-mark") << QString::fromUtf8("(́)") << 0 << 0 << 2;
    }

    void matching() {
        QFETCH(QString, text);
        QFETCH(int, cursorPosition);
        QFETCH(int, bracketPosition);
        QFETCH(int, partnerPosition);

        // Attaching to existing text must make metadata available immediately.
        QTextDocument document(text);
        BracketHighlighter highlighter(&document);
        QTextCursor cursor(&document);
        cursor.setPosition(cursorPosition);
        const auto match = findBracketMatch(cursor);
        QCOMPARE(match.position, bracketPosition);
        QCOMPARE(match.partner, partnerPosition);

        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText(text);
        moveCursor(editor, cursorPosition);
        QVector<int> expected;
        if (bracketPosition >= 0)
            expected.append(bracketPosition);
        if (partnerPosition >= 0)
            expected.append(partnerPosition);
        // Also process pending work when the expected selection is empty.
        QCoreApplication::processEvents();
        QTRY_COMPARE(highlightedPositions(editor), expected);
        for (const auto &selection : editor.extraSelections()) {
            QCOMPARE(selection.cursor.selectionEnd() - selection.cursor.selectionStart(), 1);
            QCOMPARE(selection.format.background().color(),
                     QColor(partnerPosition < 0 ? "#ffb4a9" : "#a9d9bb"));
        }
    }

    void incrementalMetadata() {
        QTextDocument document;
        document.documentLayout();
        document.setPlainText("(\nplain text\n[]\n)");
        CountingHighlighter highlighter(&document);
        const QTextBlock squareBlock = document.findBlockByNumber(2);
        auto *squareData = static_cast<BracketBlockData *>(squareBlock.userData());
        QVERIFY(squareData);
        QCOMPARE(squareData->brackets.size(), 2);
        QVERIFY(!document.findBlockByNumber(1).userData());

        QTextCursor edit(document.findBlockByNumber(1));
        edit.movePosition(QTextCursor::EndOfBlock);
        edit.insertText(" more text");
        QCOMPARE(highlighter.highlightedBlocks, QVector<int>({1}));
        QCOMPARE(squareBlock.userData(), squareData);
        QCOMPARE(squareData->brackets.at(0).position, 0);
        QCOMPARE(squareData->brackets.at(1).position, 1);

        highlighter.highlightedBlocks.clear();
        QTextCursor cursor(&document);
        for (int i = 0; i < document.characterCount(); ++i) {
            cursor.setPosition(i);
            findBracketMatch(cursor);
        }
        QCoreApplication::processEvents();
        QVERIFY(highlighter.highlightedBlocks.isEmpty());
        cursor.setPosition(0);
        QCOMPARE(findBracketMatch(cursor).partner, document.characterCount() - 2);

        // Rebuild in place, including clearing stale data after removing all brackets.
        edit = QTextCursor(squareBlock);
        edit.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        edit.insertText("{}");
        QCOMPARE(squareBlock.userData(), squareData);
        QCOMPARE(squareData->brackets.at(0).character, QChar('{'));
        QCOMPARE(squareData->brackets.at(1).character, QChar('}'));
        edit.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        edit.insertText("ab");
        QCOMPARE(squareBlock.userData(), squareData);
        QVERIFY(squareData->brackets.isEmpty());
    }

    void cursorMovesAndDocumentReplacement() {
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText("( abc )");
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 6}));
        QVERIFY(!editor.document()->isModified());
        QVERIFY(!editor.document()->isUndoAvailable());

        moveCursor(editor, 3);
        QTRY_VERIFY(editor.extraSelections().isEmpty());
        moveCursor(editor, 7);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({6, 0}));

        editor.setPlainText("[\n]");
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 2}));
        editor.clear();
        QTRY_VERIFY(editor.extraSelections().isEmpty());
    }

    void partnerEditsWithoutCursorMovement() {
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText("(\n[1]\n)");
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 6}));
        QSignalSpy cursorChanges(&editor, &QPlainTextEdit::cursorPositionChanged);

        QTextCursor edit(editor.document());
        edit.setPosition(6);
        edit.deleteChar();
        QCOMPARE(cursorChanges.count(), 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0}));
        QCOMPARE(editor.extraSelections().first().format.background().color(), QColor("#ffb4a9"));

        editor.undo();
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 6}));
        editor.redo();
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0}));

        // Several changes in one edit must be observed only in their final state.
        edit.movePosition(QTextCursor::End);
        edit.beginEditBlock();
        edit.insertText("]");
        edit.deletePreviousChar();
        edit.insertText(")");
        edit.endEditBlock();
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 6}));
    }

    void splitMergeAndUndoDeletedBlocks() {
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText("([])");
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 3}));

        QTextCursor edit(editor.document());
        edit.setPosition(2);
        edit.insertText("\n\n");
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 5}));
        moveCursor(editor, 6);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({5, 0}));
        edit.setPosition(2);
        edit.setPosition(4, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        moveCursor(editor, 4);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({3, 0}));
        editor.undo();
        moveCursor(editor, 6);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({5, 0}));
        editor.redo();
        moveCursor(editor, 4);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({3, 0}));

        editor.setPlainText("(\n[]\n)");
        edit = QTextCursor(editor.document());
        edit.setPosition(2);
        edit.setPosition(6, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0}));
        editor.undo();
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0, 5}));
        moveCursor(editor, 2);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({2, 3}));
        editor.redo();
        moveCursor(editor, 0);
        QTRY_COMPARE(highlightedPositions(editor), QVector<int>({0}));
    }

    void largeDocumentAndDeepNesting() {
        QTextDocument document;
        document.documentLayout();
        const QString text = "(\n" + QString("plain text\n").repeated(10000) + ")";
        document.setPlainText(text);
        CountingHighlighter highlighter(&document);
        QTextCursor cursor(&document);
        QCOMPARE(findBracketMatch(cursor).partner, text.size() - 1);
        cursor.movePosition(QTextCursor::End);
        QCOMPARE(findBracketMatch(cursor).partner, 0);
        QVERIFY(highlighter.highlightedBlocks.isEmpty());

        QTextCursor edit(document.findBlockByNumber(5000));
        edit.insertText("[");
        QCOMPARE(highlighter.highlightedBlocks, QVector<int>({5000}));
        cursor.setPosition(0);
        QCOMPARE(findBracketMatch(cursor).partner, -1);

        const QString nested = QString("([{").repeated(1000) + QString("}])").repeated(1000);
        document.setPlainText(nested);
        cursor.setPosition(0);
        QCOMPARE(findBracketMatch(cursor).partner, nested.size() - 1);
        cursor.movePosition(QTextCursor::End);
        QCOMPARE(findBracketMatch(cursor).partner, 0);
    }
};

QTEST_MAIN(BracketTests)
#include "brackettests.moc"
