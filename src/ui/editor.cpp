#include "editor.h"
#include "editor/blocks.h"
#include "editor/brackets.h"
#include <QFile>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCodec>
#include <QTimer>
Editor::Editor(EvaluatorRegistry &registry, QWidget *parent)
    : QPlainTextEdit(parent)
    , registry(registry)
{
    setFont(QFont("Courier New", 10));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    new BracketHighlighter(document());
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &Editor::scheduleBracketHighlight);
    connect(this, &QPlainTextEdit::textChanged, this, &Editor::scheduleBracketHighlight);
}
bool Editor::load(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        error = file.errorString();
        return false;
    }
    QTextCodec::ConverterState state;
    QString text
        = QTextCodec::codecForName("UTF-8")->toUnicode(data.constData(), data.size(), &state);
    if (state.invalidChars) {
        error = "The file is not valid UTF-8.";
        return false;
    }
    crlf = data.contains("\r\n");
    text.replace("\r\n", "\n");
    setPlainText(text);
    filePath = path;
    document()->setModified(false);
    return true;
}
bool Editor::save(const QString &path, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    QString text = toPlainText();
    if (crlf)
        text.replace("\n", "\r\n");
    QByteArray data = text.toUtf8();
    if (file.write(data) != data.size()) {
        error = file.errorString();
        return false;
    }
    if (!file.commit()) {
        error = file.errorString();
        return false;
    }
    filePath = path;
    document()->setModified(false);
    return true;
}
void Editor::keyPressEvent(QKeyEvent *event)
{
    auto cursor = textCursor();
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && event->modifiers() == Qt::NoModifier && !cursor.hasSelection() && cursor.atBlockEnd()
        && cursor.block().text().startsWith("==")) {
        auto block = locateBlock(cursor.block());
        if (!block.valid) {
            emit diagnostic(block.error);
            return;
        }
        auto result = registry.evaluate(block.evaluator, block.input);
        cursor.beginEditBlock();
        cursor.setPosition(block.insertion);
        cursor.setPosition(block.replacementEnd, QTextCursor::KeepAnchor);
        cursor.insertText("\n" + result.text + (result.text.isEmpty() ? "" : "\n") + "##");
        cursor.endEditBlock();
        setTextCursor(cursor);
        if (!result.ok)
            emit diagnostic(result.text);
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}
void Editor::scheduleBracketHighlight()
{
    if (bracketHighlightPending)
        return;
    bracketHighlightPending = true;
    // Cursor signals can arrive during an edit, before all block data is updated.
    // Coalesce them with textChanged and read the metadata after the edit finishes.
    QTimer::singleShot(0, this, [this] {
        bracketHighlightPending = false;
        highlightBrackets();
    });
}

void Editor::highlightBrackets()
{
    QList<QTextEdit::ExtraSelection> selections;
    const auto match = findBracketMatch(textCursor());
    if (match.position >= 0) {
        for (int index : {match.position, match.partner}) {
            if (index < 0)
                continue;
            QTextEdit::ExtraSelection selection;
            selection.cursor = textCursor();
            selection.cursor.setPosition(index);
            selection.cursor.setPosition(index + 1, QTextCursor::KeepAnchor);
            selection.format.setBackground(
                match.partner < 0 ? QColor("#ffb4a9") : QColor("#a9d9bb"));
            selection.format.setForeground(Qt::black);
            selections.append(selection);
        }
    }
    setExtraSelections(selections);
}
