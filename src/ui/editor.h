#pragma once
#include <QPlainTextEdit>
#include "evaluators/evaluator.h"
class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(EvaluatorRegistry &registry, QWidget *parent = nullptr);
    QString filePath;
    bool crlf = false;
    bool load(const QString &path, QString &error);
    bool save(const QString &path, QString &error);
signals:
    void diagnostic(const QString &message);
protected:
    void keyPressEvent(QKeyEvent *event) override;
private:
    EvaluatorRegistry &registry;
    bool bracketHighlightPending = false;
    void scheduleBracketHighlight();
    void highlightBrackets();
};
