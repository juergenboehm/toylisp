#pragma once

#include <QObject>
#include <QTemporaryDir>

class Editor;

class MacroTests : public QObject
{
    Q_OBJECT

    QTemporaryDir settingsDirectory;
    QString python;

    static QString writeMacro(const QString &directory, const QString &name, const QByteArray &code);

    static void select(Editor &editor, int start, int end);

private slots:
    void initTestCase();

    void init();

    void toolbarDiscoveryAndReload();

    void replacementAndUndo_data();

    void replacementAndUndo();

    void noSelectionAndIdentity();

    void failuresPreserveDocument_data();

    void failuresPreserveDocument();

    void timeout();

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
