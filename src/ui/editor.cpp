#include "editor.h"
#include "editor/blocks.h"
#include "editor/brackets.h"
#include "macro/pythonmacrojob.h"
#include <cmath>
#include <QFile>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCodec>
#include <QTimer>
#include <QWheelEvent>

namespace {
constexpr qreal normalTextPointSize = 12.0;

QString outputText(QTextDocument *document, const EvaluationBlock &block)
{
    QTextCursor cursor(document);
    cursor.setPosition(block.insertion);
    cursor.setPosition(block.replacementEnd, QTextCursor::KeepAnchor);
    return cursor.selectedText();
}
} // namespace

Editor::Editor(EvaluatorRegistry &registry, QWidget *parent)
    : QPlainTextEdit(parent)
    , registry(registry)
{
    //setFont(QFont("Courier New"));

    QFont iastFont("DejaVu Sans Mono");
    //iastFont.setStyleStrategy(QFont::PreferAntialias);

    setFont(iastFont);

    resetTextSize();
    setLineWrapMode(QPlainTextEdit::NoWrap);
    new BracketHighlighter(document());

    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &Editor::scheduleBracketHighlight);
    connect(this, &QPlainTextEdit::textChanged, this, &Editor::scheduleBracketHighlight);
    connect(this, &QPlainTextEdit::textChanged, this, &Editor::discardChangedEvaluations);
    connect(document(), &QTextDocument::contentsChange, this, &Editor::trackMacroSelection);
}

Editor::~Editor()
{
    const QSignalBlocker blocker(this);
    cancelEvaluations();
    cancelMacro();
}

bool Editor::macroIsRunning() const
{
    return pendingMacro != nullptr;
}

void Editor::runMacro(PythonMacroService &service, const QString &path)
{
    const auto cursor = textCursor();
    if (!cursor.hasSelection() || isReadOnly())
        return;
    cancelMacro();

    auto *job = new PythonMacroJob(service, this);
    pendingMacro = std::make_unique<PendingMacro>();
    pendingMacro->job = job;
    pendingMacro->start = cursor.selectionStart();
    pendingMacro->end = cursor.selectionEnd();
    pendingMacro->input = cursor.selectedText();
    QString text = pendingMacro->input;
    text.replace(QChar::ParagraphSeparator, '\n');
    text.replace(QChar::LineSeparator, '\n');

    connect(
        job,
        &PythonMacroJob::completed,
        this,
        [this, job](const QString &result, const QString &error) {
            completeMacro(job, result, error);
        });
    emit macroActivityChanged();
    job->start(path, text);
}

void Editor::cancelMacro()
{
    if (!pendingMacro)
        return;
    if (auto *job = pendingMacro->job.data()) {
        job->cancel();
        job->deleteLater();
    }
    pendingMacro.reset();
    emit macroActivityChanged();
}

void Editor::trackMacroSelection(int position, int removed, int added)
{
    if (!pendingMacro || (removed == 0 && added == 0))
        return;

    if (position + removed <= pendingMacro->start) {
        pendingMacro->start += added - removed;
        pendingMacro->end += added - removed;
    } else if (position < pendingMacro->end) {
        cancelMacro();
    }
}

void Editor::completeMacro(PythonMacroJob *job, const QString &text, const QString &error)
{
    if (!pendingMacro || pendingMacro->job != job)
        return;
    auto pending = std::move(pendingMacro);
    job->deleteLater();

    QTextCursor cursor(document());
    cursor.setPosition(pending->start);
    cursor.setPosition(pending->end, QTextCursor::KeepAnchor);
    if (!error.isEmpty()) {
        emit diagnostic("Macro failed: " + error);
    } else if (!isReadOnly() && cursor.selectedText() == pending->input) {
        QString input = pending->input;
        input.replace(QChar::ParagraphSeparator, '\n');
        input.replace(QChar::LineSeparator, '\n');
        if (text != input) {
            cursor.beginEditBlock();
            cursor.insertText(text);
            cursor.endEditBlock();
        }
    }
    emit macroActivityChanged();
}

int Editor::activeEvaluationCount() const
{
    return pendingEvaluations.size();
}

void Editor::cancelEvaluations()
{
    const auto pending = pendingEvaluations;
    pendingEvaluations.clear();
    for (const auto &evaluation : pending) {
        if (evaluation->job) {
            evaluation->job->cancel();
            evaluation->job->deleteLater();
        }
    }

    if (!pending.isEmpty())
        emit evaluationActivityChanged();
}

bool Editor::evaluationIsCurrent(const PendingEvaluation &pending) const
{
    const auto markerBlock = pending.marker.block();
    if (markerBlock.text() != pending.markerText)
        return false;

    const auto block = locateBlock(markerBlock);
    return block.valid && block.input == pending.input && block.evaluator == pending.evaluator
           && outputText(document(), block) == pending.output;
}

void Editor::discardChangedEvaluations()
{
    if (evaluationOutputChanging)
        return;
    const auto pending = pendingEvaluations;

    bool changed = false;
    for (const auto &evaluation : pending) {
        if (!evaluationIsCurrent(*evaluation)) {
            pendingEvaluations.removeOne(evaluation);
            if (evaluation->job) {
                evaluation->job->cancel();
                evaluation->job->deleteLater();
            }
            changed = true;
        }
    }

    if (changed)
        emit evaluationActivityChanged();
}

void Editor::completeEvaluation(const std::shared_ptr<PendingEvaluation> &pending, bool immediate)
{
    if (!pendingEvaluations.removeOne(pending))
        return;

    auto *job = pending->job.data();
    if (job && !job->isCancelled() && evaluationIsCurrent(*pending)) {
        const auto result = job->result();
        const auto block = locateBlock(pending->marker.block());
        QTextCursor cursor(document());

        evaluationOutputChanging = true;
        cursor.beginEditBlock();
        cursor.setPosition(block.insertion);
        cursor.setPosition(block.replacementEnd, QTextCursor::KeepAnchor);
        cursor.insertText("\n" + result.text + (result.text.isEmpty() ? "" : "\n") + "##");
        cursor.endEditBlock();
        evaluationOutputChanging = false;

        if (immediate)
            setTextCursor(cursor);
        if (!result.ok)
            emit diagnostic(result.text);
    }

    if (job)
        job->deleteLater();
    discardChangedEvaluations();
    emit evaluationActivityChanged();
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

        // Re-evaluation supersedes the previous request for this marker only.
        const auto previous = pendingEvaluations;
        for (const auto &pending : previous) {
            if (pending->marker.block() == cursor.block()) {
                pendingEvaluations.removeOne(pending);
                if (pending->job) {
                    pending->job->cancel();
                    pending->job->deleteLater();
                }
            }
        }

        auto pending = std::make_shared<PendingEvaluation>();
        pending->marker = QTextCursor(cursor.block());
        pending->marker.setKeepPositionOnInsert(true);
        pending->markerText = cursor.block().text();
        pending->input = block.input;
        pending->evaluator = block.evaluator;
        pending->output = outputText(document(), block);

        pending->job = registry.startEvaluation(block.evaluator, block.input, this);
        pendingEvaluations.append(pending);
        connect(pending->job, &EvaluationJob::completed, this, [this, pending] {
            completeEvaluation(pending, false);
        });

        if (pending->job->isFinished())
            completeEvaluation(pending, true);
        else
            emit evaluationActivityChanged();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);
}

void Editor::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() != Qt::ControlModifier) {
        QPlainTextEdit::wheelEvent(event);
        return;
    }

    // Keep fractional wheel steps. Pixel-only touchpads use 40 pixels per zoom step.
    qreal steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0)
        steps = event->pixelDelta().y() / 40.0;
    if (steps != 0.0)
        setTextPointSize(font().pointSizeF() * std::pow(1.05, steps));
    event->accept();
}

void Editor::resetTextSize()
{
    setTextPointSize(normalTextPointSize);
}

void Editor::setTextPointSize(qreal pointSize)
{
    auto editorFont = font();
    editorFont.setPointSizeF(qBound(1.0, pointSize, 96.0));
    setFont(editorFont);
    setTabStopDistance(QFontMetricsF(editorFont).horizontalAdvance(' ') * 4);
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
