#pragma once

#include "evaluators/evaluator.h"
#include <memory>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>

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

signals:
    void diagnostic(const QString &message);

    void evaluationActivityChanged();

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
    bool bracketHighlightPending = false;
    bool evaluationOutputChanging = false;

    bool evaluationIsCurrent(const PendingEvaluation &pending) const;

    void discardChangedEvaluations();

    void completeEvaluation(const std::shared_ptr<PendingEvaluation> &pending, bool immediate);

    void setTextPointSize(qreal pointSize);

    void scheduleBracketHighlight();

    void highlightBrackets();
};
