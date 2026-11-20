#pragma once

#include "evaluators/evaluator.h"
#include <memory>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>

class PythonMacroJob;

class Editor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit Editor(EvaluatorRegistry &registry, QWidget *parent = nullptr);

    ~Editor() override;

    QString filePath;
    bool crlf = false;

    bool load(const QString &path, QString &error);

    bool save(const QString &path, QString &error);

    void resetTextSize();

    void cancelEvaluations();

    int activeEvaluationCount() const;

    void runMacro(const QString &interpreter, const QString &path);

    void cancelMacro();

    bool macroIsRunning() const;

signals:
    void diagnostic(const QString &message);

    void evaluationActivityChanged();

    void macroActivityChanged();

protected:
    void keyPressEvent(QKeyEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

private:
    EvaluatorRegistry &registry;

    struct PendingEvaluation
    {
        QPointer<EvaluationJob> job;
        QTextCursor marker;
        QString input;
        QString evaluator;
        QString markerText;
        QString output;
    };

    QList<std::shared_ptr<PendingEvaluation>> pendingEvaluations;

    struct PendingMacro
    {
        QPointer<PythonMacroJob> job;
        int start;
        int end;
        QString input;
    };

    std::unique_ptr<PendingMacro> pendingMacro;
    bool bracketHighlightPending = false;
    bool evaluationOutputChanging = false;

    bool evaluationIsCurrent(const PendingEvaluation &pending) const;

    void discardChangedEvaluations();

    void completeEvaluation(const std::shared_ptr<PendingEvaluation> &pending, bool immediate);

    void trackMacroSelection(int position, int removed, int added);

    void completeMacro(PythonMacroJob *job, const QString &text, const QString &error);

    void setTextPointSize(qreal pointSize);

    void scheduleBracketHighlight();

    void highlightBrackets();
};
