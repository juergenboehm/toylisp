#include "macrotests.h"
#include "macro/pythonmacrojob.h"
#include "ui/editor.h"
#include "ui/macroselector.h"
#include "ui/mainwindow.h"
#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QtTest>

QString MacroTests::writeMacro(const QString &directory, const QString &name, const QByteArray &code)
{
    const QString path = directory + "/" + name + ".py";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(code) != code.size())
        qFatal("Cannot create temporary test macro");
    return path;
}

void MacroTests::select(Editor &editor, int start, int end)
{
    QTextCursor cursor(editor.document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
}

void MacroTests::initTestCase()
{
    QVERIFY(settingsDirectory.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    python = QStandardPaths::findExecutable("python3");
    if (python.isEmpty())
        QSKIP("Python 3 is needed for macro tests");
}

void MacroTests::init()
{
    QSettings("ToyLisp", "ToyLisp").clear();
}

void MacroTests::toolbarDiscoveryAndReload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindow window(directory.path());
    auto *selector = window.findChild<MacroSelector *>("macroSelector");
    auto *action = window.findChild<QAction *>("runMacroAction");
    auto *editor = window.findChild<Editor *>();
    QVERIFY(selector);
    QVERIFY(action);
    QVERIFY(editor);
    QCOMPARE(selector->count(), 0);
    QVERIFY(!action->isEnabled());

    writeMacro(directory.path(), "z", "def macro(txt): return 'old'\n");
    const QString first = writeMacro(directory.path(), "a", "def macro(txt): return txt\n");
    QFile ignored(directory.path() + "/ignored.txt");
    QVERIFY(ignored.open(QIODevice::WriteOnly));
    ignored.close();
    selector->showPopup();
    QCOMPARE(selector->count(), 2);
    QCOMPARE(selector->itemText(0), QString("a"));
    QCOMPARE(selector->itemText(1), QString("z"));
    selector->hidePopup();
    QVERIFY(!action->isEnabled());

    editor->setPlainText("original");
    editor->selectAll();
    QVERIFY(action->isEnabled());
    selector->setCurrentIndex(1);
    selector->showPopup();
    QCOMPARE(selector->currentText(), QString("z"));
    selector->hidePopup();

    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", python);
    action->trigger();
    QVERIFY(editor->macroIsRunning());
    QVERIFY(!action->isEnabled());
    auto *status = window.findChild<QLabel *>("macroStatus");
    QCOMPARE(status->text(), QString("Running macros: 1"));
    QTRY_COMPARE(editor->toPlainText(), QString("old"));
    QVERIFY(!editor->macroIsRunning());
    QVERIFY(status->text().isEmpty());

    // Same-size rewrites within one timestamp tick must use the new source.
    writeMacro(directory.path(), "z", "def macro(txt): return 'new'\n");
    editor->selectAll();
    action->trigger();
    QTRY_COMPARE(editor->toPlainText(), QString("new"));
    QVERIFY(QFile::remove(first));
    QVERIFY(QFile::remove(directory.path() + "/z.py"));
    selector->showPopup();
    QCOMPARE(selector->count(), 0);
    QVERIFY(!action->isEnabled());
    selector->hidePopup();
}

void MacroTests::replacementAndUndo_data()
{
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<bool>("reverse");
    QTest::newRow("unicode-and-newlines") << QString::fromUtf8("ā राम\nsecond\n") << false;
    QTest::newRow("reverse-selection") << QString("replacement") << true;
    QTest::newRow("delete-selection") << QString() << false;
}

void MacroTests::replacementAndUndo()
{
    QFETCH(QString, replacement);
    QFETCH(bool, reverse);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // repr via a JSON string is valid Python for these string fixtures.
    const QByteArray literal = QJsonDocument(QJsonArray{replacement}).toJson(QJsonDocument::Compact);
    const QString path = writeMacro(
        directory.path(),
        "replace",
        "def macro(txt):\n    assert txt == 'राम\\nend'\n"
        "    print('macro diagnostic')\n    return "
            + literal.mid(1, literal.size() - 2) + "\n");
    EvaluatorRegistry registry;
    Editor editor(registry);
    const QString input = QString::fromUtf8("prefix राम\nend suffix");
    editor.setPlainText(input);
    select(editor, reverse ? 14 : 7, reverse ? 7 : 14);
    QSignalSpy errors(&editor, &Editor::diagnostic);
    editor.runMacro(python, path);
    QCOMPARE(editor.toPlainText(), input);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(errors.size(), 0);
    QCOMPARE(editor.toPlainText(), "prefix " + replacement + " suffix");
    QVERIFY(editor.document()->isModified());
    editor.undo();
    QCOMPARE(editor.toPlainText(), input);
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.redo();
    QCOMPARE(editor.toPlainText(), "prefix " + replacement + " suffix");

    // An identity macro must preserve an already existing undo history.
    const QString identity
        = writeMacro(directory.path(), "identity", "def macro(txt): return txt\n");
    editor.selectAll();
    editor.runMacro(python, identity);
    QTRY_VERIFY(!editor.macroIsRunning());
    editor.undo();
    QCOMPARE(editor.toPlainText(), input);
}

void MacroTests::noSelectionAndIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(directory.path(), "identity", "def macro(txt): return txt\n");
    EvaluatorRegistry registry;
    Editor editor(registry);
    QSignalSpy errors(&editor, &Editor::diagnostic);
    editor.setPlainText("unchanged\n");
    editor.runMacro(python, path);
    QVERIFY(!editor.macroIsRunning());
    QCOMPARE(errors.size(), 0);
    editor.selectAll();
    editor.runMacro(python, path);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.toPlainText(), QString("unchanged\n"));
    QCOMPARE(
        editor.textCursor().selectedText(), QString("unchanged") + QChar(QChar::ParagraphSeparator));
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());

    const QString softLineBreak = QString("first") + QChar(QChar::LineSeparator) + "second";
    editor.setPlainText(softLineBreak);
    editor.selectAll();
    editor.runMacro(python, path);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.textCursor().selectedText(), softLineBreak);
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());

    const QString check = writeMacro(
        directory.path(),
        "linebreaks",
        "def macro(txt):\n    assert txt == 'first\\nsecond'\n    return 'normalized'\n");
    editor.runMacro(python, check);
    QTRY_COMPARE(editor.toPlainText(), QString("normalized"));
}

void MacroTests::failuresPreserveDocument_data()
{
    QTest::addColumn<QByteArray>("code");
    QTest::addColumn<QString>("expected");
    QTest::newRow("exception") << QByteArray("def macro(txt): raise ValueError('test failure')\n")
                               << "test failure";
    QTest::newRow("wrong-type") << QByteArray("def macro(txt): return 42\n")
                                << "must return a string";
    QTest::newRow("missing-function") << QByteArray("value = 42\n") << "macro(txt)";
    QTest::newRow("syntax-error") << QByteArray("def macro(:\n") << "SyntaxError";
    QTest::newRow("import-error") << QByteArray("import nonexistent_toylisp_test_module\n")
                                  << "ModuleNotFoundError";
    QTest::newRow("system-exit") << QByteArray("def macro(txt): raise SystemExit('macro exit')\n")
                                 << "SystemExit";
    QTest::newRow("crash") << QByteArray("import os\ndef macro(txt): os._exit(7)\n")
                           << "Process exited";
    QTest::newRow("oversize-response")
        << QByteArray("def macro(txt): return 'a' * (256*1024)\n") << "size limit";
    QTest::newRow("no-interpreter")
        << QByteArray("def macro(txt): return txt\n") << "Set a Python interpreter";
    QTest::newRow("bad-interpreter")
        << QByteArray("def macro(txt): return txt\n") << "Macro failed:";
    QTest::newRow("missing-file") << QByteArray("def macro(txt): return txt\n")
                                  << "FileNotFoundError";
    QTest::newRow("oversize-input") << QByteArray("def macro(txt): return txt\n") << "size limit";
}

void MacroTests::failuresPreserveDocument()
{
    QFETCH(QByteArray, code);
    QFETCH(QString, expected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QString path = writeMacro(directory.path(), "bad", code);
    QString executable = python;
    const QString row = QString::fromLatin1(QTest::currentDataTag());
    if (row == "no-interpreter")
        executable.clear();
    else if (row == "bad-interpreter")
        executable = directory.path() + "/missing-python";
    else if (row == "missing-file")
        path += ".missing";
    EvaluatorRegistry registry;
    Editor editor(registry);
    const QString input = row == "oversize-input" ? QString(256 * 1024, 'x') : QString("original");
    editor.setPlainText(input);
    editor.selectAll();
    QSignalSpy errors(&editor, &Editor::diagnostic);
    editor.runMacro(executable, path);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(errors.size(), 1);
    QVERIFY2(
        errors.first().first().toString().contains(expected),
        qPrintable(errors.first().first().toString()));
    QCOMPARE(editor.toPlainText(), input);
    QCOMPARE(editor.textCursor().selectedText(), input);
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());

    if (row == "oversize-input") {
        editor.setPlainText("smaller input");
        editor.selectAll();
    }
    path = writeMacro(directory.path(), "good", "def macro(txt): return 'recovered'\n");
    editor.runMacro(python, path);
    QTRY_COMPARE(editor.toPlainText(), QString("recovered"));
}

void MacroTests::timeout()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(), "slow", "import time\ndef macro(txt): time.sleep(2); return txt\n");
    PythonMacroJob job(nullptr, 50);
    QSignalSpy completion(&job, &PythonMacroJob::completed);
    job.start(python, path, "original");
    QTRY_COMPARE(completion.size(), 1);
    QVERIFY(completion.first().at(1).toString().contains("timed out"));
}

void MacroTests::unrelatedEditsAndCursor()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(),
        "slow",
        "import time\ndef macro(txt): time.sleep(0.3); return txt.upper()\n");
    EvaluatorRegistry registry;
    Editor editor(registry);
    editor.setPlainText("before selected after");
    select(editor, 7, 15);
    editor.runMacro(python, path);
    QTextCursor edit(editor.document());
    edit.setPosition(0);
    edit.insertText("prefix ");
    edit.movePosition(QTextCursor::End);
    edit.insertText(" suffix");
    select(editor, 0, 6);
    bool responsive = false;
    QTimer::singleShot(0, &editor, [&] { responsive = editor.macroIsRunning(); });
    QTRY_VERIFY(responsive);
    QVERIFY(editor.macroIsRunning());
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.toPlainText(), QString("prefix before SELECTED after suffix"));
    QCOMPARE(editor.textCursor().selectedText(), QString("prefix"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString("prefix before selected after suffix"));
}

void MacroTests::selectionBoundaries_data()
{
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("end");
    QTest::addColumn<QString>("edit");
    QTest::addColumn<QString>("expected");
    QTest::newRow("insert-at-start") << 3 << 3 << "x" << "ab xCD ef";
    QTest::newRow("insert-at-end") << 5 << 5 << "x" << "ab CDx ef";
    QTest::newRow("delete-up-to-start") << 0 << 3 << "" << "CD ef";
    QTest::newRow("delete-from-end") << 5 << 8 << "" << "ab CD";
}

void MacroTests::selectionBoundaries()
{
    QFETCH(int, start);
    QFETCH(int, end);
    QFETCH(QString, edit);
    QFETCH(QString, expected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(),
        "slow",
        "import time\ndef macro(txt): time.sleep(0.1); return txt.upper()\n");
    EvaluatorRegistry registry;
    Editor editor(registry);
    editor.setPlainText("ab cd ef");
    select(editor, 3, 5);
    editor.runMacro(python, path);
    select(editor, start, end);
    editor.insertPlainText(edit);
    QVERIFY(editor.macroIsRunning());
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.toPlainText(), expected);
}

void MacroTests::changedSelection_data()
{
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("end");
    QTest::addColumn<bool>("undo");
    QTest::newRow("insert-within") << 10 << 10 << false;
    QTest::newRow("remove-within") << 10 << 11 << false;
    QTest::newRow("overlap-start") << 6 << 8 << false;
    QTest::newRow("overlap-end") << 14 << 16 << false;
    QTest::newRow("replace-all") << 0 << 21 << false;
    QTest::newRow("undo-does-not-revive") << 10 << 11 << true;
}

void MacroTests::changedSelection()
{
    QFETCH(int, start);
    QFETCH(int, end);
    QFETCH(bool, undo);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(),
        "slow",
        "import time\nfrom pathlib import Path\ndef macro(txt):\n"
        "    Path(__file__).with_suffix('.started').touch()\n"
        "    time.sleep(0.3)\n"
        "    Path(__file__).with_suffix('.finished').touch()\n    return 'STALE'\n");
    EvaluatorRegistry registry;
    Editor editor(registry);
    editor.setPlainText("before selected after");
    select(editor, 7, 15);
    QSignalSpy errors(&editor, &Editor::diagnostic);
    editor.runMacro(python, path);
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.started"));
    select(editor, start, end);
    editor.insertPlainText("x");
    QVERIFY(!editor.macroIsRunning());
    if (undo)
        editor.undo();
    const QString edited = editor.toPlainText();
    QTest::qWait(450);
    QCOMPARE(editor.toPlainText(), edited);
    QVERIFY(!QFile::exists(directory.path() + "/slow.finished"));
    QCOMPARE(errors.size(), 0);
}

void MacroTests::tabSwitchAndClose()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeMacro(
        directory.path(),
        "slow",
        "import time\ndef macro(txt): time.sleep(0.3); return txt.upper()\n");
    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", python);
    MainWindow window(directory.path());
    auto *first = window.findChild<Editor *>();
    auto *tabs = qobject_cast<QTabWidget *>(first->parentWidget()->parentWidget());
    auto *run = window.findChild<QAction *>("runMacroAction");
    QVERIFY(tabs);
    first->setPlainText("first");
    first->selectAll();
    run->trigger();
    for (auto *action : window.findChildren<QAction *>()) {
        if (action->shortcut() == QKeySequence::New)
            action->trigger();
    }
    QCOMPARE(tabs->count(), 2);
    auto *second = qobject_cast<Editor *>(tabs->currentWidget());
    QVERIFY(second != first);
    second->setPlainText("second");
    QVERIFY(!run->isEnabled());
    QTRY_COMPARE(first->toPlainText(), QString("FIRST"));
    QCOMPARE(second->toPlainText(), QString("second"));

    second->selectAll();
    run->trigger();
    QVERIFY(second->macroIsRunning());
    QPointer<Editor> closed(second);
    QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, tabs->currentIndex())));
    QCOMPARE(tabs->count(), 1);
    QTRY_VERIFY(closed.isNull());
    QTest::qWait(450);
    QCOMPARE(first->toPlainText(), QString("FIRST"));
}

void MacroTests::builtinMacro_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("one-line") << QString::fromUtf8("नमस्ते") << QString::fromUtf8("नमस्ते\nnamaste");
    QTest::newRow("mixed-lines") << QString::fromUtf8("Title\nनमस्ते\n\nरामः\nend")
                                 << QString::fromUtf8("Title\nनमस्ते\nnamaste\n\nरामः\nrāmaḥ\nend");
    QTest::newRow("mixed-script") << QString::fromUtf8("राम text\nराम தமிழ்\nराम 123\n123\n॥\n\t")
                                  << QString::fromUtf8("राम text\nराम தமிழ்\nराम 123\n123\n॥\n\t");
    QTest::newRow("whitespace-and-punctuation")
        << QString::fromUtf8("\tरामः कृष्णः। १॥\n")
        << QString::fromUtf8("\tरामः कृष्णः। १॥\n\trāmaḥ kṛṣṇaḥ| 1||\n");
    QTest::newRow("crlf") << QString::fromUtf8("नमस्ते\r\nरामः\r\n")
                          << QString::fromUtf8("नमस्ते\r\nnamaste\r\nरामः\r\nrāmaḥ\r\n");
    QTest::newRow("vowel-length-and-virama")
        << QString::fromUtf8("दूरतस्\nभु\nभू") << QString::fromUtf8("दूरतस्\ndūratas\nभु\nbhu\nभू\nbhū");
}

void MacroTests::builtinMacro()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    const QString executable = qEnvironmentVariable("TOYLISP_TEST_PYTHON", python);
    QProcess check;
    check.start(executable, {"-I", "-c", "from indic_transliteration import detect, sanscript"});
    QVERIFY(check.waitForFinished(5000));
    if (check.exitCode() != 0)
        QSKIP("Set TOYLISP_TEST_PYTHON to Python with indic-transliteration for the probe macro");
    const QString path = QFINDTESTDATA("../src/macro/python/devanagari_iast.py");
    QVERIFY(!path.isEmpty());
    PythonMacroJob job;
    QSignalSpy completion(&job, &PythonMacroJob::completed);
    job.start(executable, path, input);
    QTRY_COMPARE(completion.size(), 1);
    QVERIFY2(
        completion.first().at(1).toString().isEmpty(),
        qPrintable(completion.first().at(1).toString()));
    QCOMPARE(completion.first().at(0).toString(), expected);
}

void MacroTests::macroExampleAndScreenshot()
{
    const QString executable = qEnvironmentVariable("TOYLISP_TEST_PYTHON", python);
    QProcess check;
    check.start(executable, {"-I", "-c", "from indic_transliteration import detect, sanscript"});
    QVERIFY(check.waitForFinished(5000));
    if (check.exitCode() != 0)
        QSKIP("Set TOYLISP_TEST_PYTHON to Python with indic-transliteration for the probe macro");

    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", executable);
    MainWindow window;
    window.show();
    auto *selector = window.findChild<MacroSelector *>("macroSelector");
    auto *run = window.findChild<QAction *>("runMacroAction");
    auto *editor = window.findChild<Editor *>();
    selector->setCurrentIndex(selector->findText("devanagari_iast"));
    QVERIFY(selector->currentIndex() >= 0);
    const QString input = QString::fromUtf8("नमस्ते\nरामः कृष्णः।\nभू\n");
    editor->setPlainText(input);
    editor->selectAll();
    run->trigger();
    QTRY_VERIFY(!editor->macroIsRunning());
    const QString result = QString::fromUtf8("नमस्ते\nnamaste\nरामः कृष्णः।\nrāmaḥ kṛṣṇaḥ|\nभू\nbhū\n");
    QCOMPARE(editor->toPlainText(), result);
    editor->undo();
    QCOMPARE(editor->toPlainText(), input);
    editor->redo();
    QCOMPARE(editor->toPlainText(), result);
    QVERIFY(window.grab().save("toylisp-macro-screenshot.png"));
    editor->document()->setModified(false);
}

QTEST_MAIN(MacroTests)
