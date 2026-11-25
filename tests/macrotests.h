#pragma once

#include "macro/pythonmacroservice.h"
#include <QObject>
#include <QTemporaryDir>

class Editor;

class MacroTests : public QObject
{
    Q_OBJECT

    QTemporaryDir settingsDirectory;
    QString python;
    PythonMacroService service;

    static QString writeMacro(const QString &directory, const QString &name, const QByteArray &code);

    static void select(Editor &editor, int start, int end);

    static void evaluateBlock(Editor &editor, const QString &marker = "==macro");

private slots:
    void initTestCase();

    void init();

    void toolbarDiscoveryAndReload();

    void evaluatorReplacementAndUndo_data();

    void evaluatorReplacementAndUndo();

    void evaluatorFailures();

    void evaluatorSelectionAndContextSnapshot();

    void evaluatorCancellationAndRestart();

    void replacementAndUndo_data();

    void replacementAndUndo();

    void noSelectionAndIdentity();

    void failuresPreserveDocument_data();

    void failuresPreserveDocument();

    void timeout();

    void sharedRuntimeAndReload();

    void softCancellationAndQueue();

    void restartDiscardsRequests();

    void failuresRetainRuntime();

    void queueLimit();

    void runtimeGeminiExample();

    void unrelatedEditsAndCursor();

    void selectionBoundaries_data();

    void selectionBoundaries();

    void changedSelection_data();

    void changedSelection();

    void tabSwitchAndClose();

    void builtinMacro_data();

    void builtinMacro();

    void macroExampleAndScreenshot();
};
