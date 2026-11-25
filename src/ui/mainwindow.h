#pragma once

#include "evaluators/evaluator.h"
#include "macro/pythonmacroservice.h"
#include "sanskrit/sanskrit.h"
#include "sanskrit/sanskritserviceqt.h"
#include <QMainWindow>

class Editor;
class QTabWidget;
class QPlainTextEdit;
class QLabel;
class QAction;
class MacroSelector;
class MacroEvaluator;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &macroDirectory = {}, const QString &runtimeDirectory = {});

    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    sanskrit::SanskritServiceQt sanskritService;
    sanskrit::Sanskrit sanskrit{sanskritService};
    EvaluatorRegistry registry;
    PythonMacroService macroService;
    MacroEvaluator *macroEvaluator;
    QTabWidget *editors;
    QPlainTextEdit *output;
    QLabel *lookupStatus;
    QLabel *macroStatus;
    MacroSelector *macroSelector;
    QAction *runMacroAction;
    int untitledCount = 0;

    Editor *currentEditor() const;

    Editor *addEditor();

    void openFile();

    bool saveEditor(Editor *editor, bool saveAs = false);

    bool confirmClose(Editor *editor);

    void closeTab(int index);

    void updateTitle(Editor *editor);

    void findReplace();

    void cancelPendingWork();

    void updateEvaluationActivity();

    void updateMacroActivity();

    void choosePythonInterpreter();
};
