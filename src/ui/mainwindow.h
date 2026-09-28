#pragma once
#include <QMainWindow>
#include "evaluators/evaluator.h"
class Editor;
class QTabWidget;
class QPlainTextEdit;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    EvaluatorRegistry registry;
    QTabWidget *editors;
    QPlainTextEdit *output;
    int untitledCount = 0;
    Editor *currentEditor() const;
    Editor *addEditor();
    void openFile();
    bool saveEditor(Editor *editor, bool saveAs = false);
    bool confirmClose(Editor *editor);
    void closeTab(int index);
    void updateTitle(Editor *editor);
    void findReplace();
};
