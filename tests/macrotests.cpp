#include "macrotests.h"
#include "evaluators/macro/macroevaluator.h"
#include "macro/pythonmacrojob.h"
#include "ui/editor.h"
#include "ui/macroselector.h"
#include "ui/mainwindow.h"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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

void MacroTests::evaluateBlock(Editor &editor, const QString &marker)
{
    QTextCursor cursor(editor.document());
    const int position = editor.toPlainText().indexOf(marker);
    QVERIFY(position >= 0);
    cursor.setPosition(position + marker.size());
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_Return);
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
    service.restart();
    service.setInterpreter(python);
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
    QCOMPARE(status->text(), QString("Pending macros: 1"));
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

void MacroTests::evaluatorReplacementAndUndo_data()
{
    QTest::addColumn<QString>("replacement");
    QTest::newRow("unicode-multiline") << QString::fromUtf8("rāma राम\nsecond");
    QTest::newRow("empty") << QString();
    QTest::newRow("trailing-newline") << QString("first\n");
}

void MacroTests::evaluatorReplacementAndUndo()
{
    QFETCH(QString, replacement);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QByteArray literal
        = QJsonDocument(QJsonArray{replacement}).toJson(QJsonDocument::Compact);
    const QString path = writeMacro(
        directory.path(), "block",
        QByteArray("def macro(txt):\n    assert txt == 'राम\\nend'\n    return ") + literal
            + "[0]\n");
    EvaluatorRegistry registry;
    auto evaluator = std::make_unique<MacroEvaluator>(service);
    evaluator->setMacro(path);
    registry.addEvaluator("macro", std::move(evaluator));
    Editor editor(registry);
    const QString before = "before\n>>\nराम\nend\n==macro\nold\n##\ntail\n>>\n1+2\n==arith\n3\n##";
    editor.setPlainText(before);
    evaluateBlock(editor);
    QVERIFY(editor.macroIsRunning());
    QVERIFY(editor.activeEvaluationCount() == 1);
    editor.moveCursor(QTextCursor::Start);
    QTRY_VERIFY(!editor.macroIsRunning());
    const QString after = "before\n>>\nराम\nend\n==macro\n" + replacement
                          + (replacement.isEmpty() ? "" : "\n")
                          + "##\ntail\n>>\n1+2\n==arith\n3\n##";
    QCOMPARE(editor.toPlainText(), after);
    QCOMPARE(editor.textCursor().position(), 0);
    editor.undo();
    QCOMPARE(editor.toPlainText(), before);
    QVERIFY(!editor.document()->isModified());
    editor.redo();
    QCOMPARE(editor.toPlainText(), after);
}

void MacroTests::evaluatorFailures()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    EvaluatorRegistry registry;
    auto evaluator = std::make_unique<MacroEvaluator>(service);
    auto *macro = evaluator.get();
    registry.addEvaluator("macro", std::move(evaluator));
    Editor editor(registry);
    QSignalSpy diagnostics(&editor, &Editor::diagnostic);
    editor.setPlainText(">>\ninput\n==macro\nold\n##");
    evaluateBlock(editor);
    QVERIFY(editor.toPlainText().contains("Select a Python macro in the toolbar\n##"));
    QCOMPARE(diagnostics.size(), 1);
    const QString path = writeMacro(directory.path(), "bad", "def macro(txt): return 42\n");
    macro->setMacro(path);
    editor.setPlainText(">>\ninput\n==macro\nold\n##");
    evaluateBlock(editor);
    QTRY_VERIFY(!editor.macroIsRunning());
    QVERIFY(editor.toPlainText().contains(
        "Macro failed: TypeError: macro(txt) must return a string\n##"));
    QCOMPARE(diagnostics.size(), 2);
    editor.undo();
    QCOMPARE(editor.toPlainText(), QString(">>\ninput\n==macro\nold\n##"));
}

void MacroTests::evaluatorSelectionAndContextSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkdir("python"));
    writeMacro(directory.path(), "macro_runtime",
               "from macro_context import get_context\ncount = 0\n"
               "def context():\n    c = get_context()\n"
               "    return c['result_language'] + ':' + c['dictionary']\n");
    const QByteArray code
        = "import time\nfrom pathlib import Path\nimport macro_runtime as runtime\n"
          "def macro(txt):\n    runtime.count += 1\n    if txt == 'hold':\n"
          "        Path(__file__).with_suffix('.started').touch()\n        time.sleep(0.5)\n"
          "    return 'LABEL:' + str(runtime.count) + ':' + runtime.context()\n";
    writeMacro(root.filePath("python"), "a", QByteArray(code).replace("LABEL", "a"));
    writeMacro(root.filePath("python"), "b", QByteArray(code).replace("LABEL", "b"));
    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", python);
    MainWindow window(root.filePath("python"), directory.path());
    auto *first = window.findChild<Editor *>();
    auto *tabs = qobject_cast<QTabWidget *>(first->parentWidget()->parentWidget());
    auto *selector = window.findChild<MacroSelector *>("macroSelector");
    auto *evaluators = window.findChild<QComboBox *>("evaluatorSelector");
    QVERIFY(evaluators->findData("sanskrit") >= 0);
    QVERIFY(evaluators->findData("sanskrit-verse") >= 0);
    QCOMPARE(evaluators->itemText(evaluators->findData("macro")), QString("Macro"));
    auto *run = window.findChild<QAction *>("runMacroAction");
    first->setPlainText("seed");
    first->selectAll();
    run->trigger();
    QTRY_COMPARE(first->toPlainText(), QString("a:1:de:pwg"));
    first->setPlainText(">>\nhold\n==macro\nold\n##");
    evaluateBlock(*first);
    QTRY_VERIFY(QFile::exists(root.filePath("python/a.started")));
    for (auto *action : window.findChildren<QAction *>()) {
        if (action->shortcut() == QKeySequence::New)
            action->trigger();
    }
    auto *second = qobject_cast<Editor *>(tabs->currentWidget());
    QVERIFY(second != first);
    window.findChild<QAction *>("sanskritLanguage_en")->trigger();
    second->setPlainText(">>\nqueued\n==macro\nold\n##");
    evaluateBlock(*second);
    QVERIFY(second->macroIsRunning());
    QCOMPARE(window.findChild<QLabel *>("macroStatus")->text(), QString("Pending macros: 2"));
    selector->setCurrentIndex(selector->findText("b"));
    window.findChild<QAction *>("sanskritLanguage_de")->trigger();
    QTRY_VERIFY(!second->macroIsRunning());
    QVERIFY(first->toPlainText().contains("==macro\na:2:de:pwg\n##"));
    QVERIFY(second->toPlainText().contains("==macro\na:3:en:mw\n##"));
    // A bare marker also uses Macro when it is the global default.
    evaluators->setCurrentIndex(evaluators->findData("macro"));
    second->setPlainText(">>\nnext\n==\nold\n##");
    evaluateBlock(*second, "==");
    QTRY_VERIFY(!second->macroIsRunning());
    QVERIFY(second->toPlainText().contains("==\nb:4:de:pwg\n##"));
    // Changing the default and reevaluating the same bare marker supersedes Macro.
    first->setPlainText(">>\nhold\n==\nold\n##");
    evaluateBlock(*first, "==");
    QTRY_VERIFY(QFile::exists(root.filePath("python/b.started")));
    evaluators->setCurrentIndex(evaluators->findData("arith"));
    evaluateBlock(*first, "==");
    QVERIFY(!first->macroIsRunning());
    QVERIFY(window.findChild<QLabel *>("macroStatus")->text().isEmpty());
    QVERIFY(first->toPlainText().contains("Unknown variable"));
}

void MacroTests::evaluatorCancellationAndRestart()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(), "slow",
        "import time\nfrom pathlib import Path\ndef macro(txt):\n"
        "    Path(__file__).with_suffix('.started').touch()\n"
        "    time.sleep(0.3)\n    return txt.upper()\n");
    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", python);
    MainWindow window(directory.path());
    auto *editor = window.findChild<Editor *>();
    const QString before = ">>\ninput\n==macro\nold\n##\ntail";
    editor->setPlainText(before);
    evaluateBlock(*editor);
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.started"));
    // Direct selection execution cannot queue a second macro in the same tab.
    editor->selectAll();
    QVERIFY(!window.findChild<QAction *>("runMacroAction")->isEnabled());
    QSignalSpy diagnostics(editor, &Editor::diagnostic);
    editor->runMacro(service, path);
    QCOMPARE(editor->activeEvaluationCount(), 1);
    // An unrelated edit survives completion without moving the cursor.
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText("!");
    QTRY_VERIFY(!editor->macroIsRunning());
    QVERIFY(editor->toPlainText().endsWith("tail!"));
    QVERIFY(editor->toPlainText().contains("==macro\nINPUT\n##"));
    QCOMPARE(editor->textCursor().position(), editor->toPlainText().size());
    // Changing input cancels the evaluation, even if the edit is undone.
    editor->setPlainText(before);
    evaluateBlock(*editor);
    select(*editor, 3, 4);
    editor->insertPlainText("X");
    QVERIFY(!editor->macroIsRunning());
    editor->undo();
    evaluateBlock(*editor);
    window.findChild<QAction *>("restartMacroInterpreterAction")->trigger();
    QVERIFY(!editor->macroIsRunning());
    QCOMPARE(editor->activeEvaluationCount(), 0);
    QCOMPARE(editor->toPlainText(), before);
    QVERIFY(diagnostics.isEmpty());
    // The restart remains lazy and the next evaluator request recovers.
    evaluateBlock(*editor);
    QTRY_VERIFY(!editor->macroIsRunning());
    QVERIFY(editor->toPlainText().contains("==macro\nINPUT\n##"));
    evaluateBlock(*editor);
    window.findChild<QAction *>("cancelEvaluationsAction")->trigger();
    QVERIFY(!editor->macroIsRunning());
    QCOMPARE(editor->activeEvaluationCount(), 0);
    editor->document()->setModified(false);
    auto *evaluators = window.findChild<QComboBox *>("evaluatorSelector");
    evaluators->setCurrentIndex(evaluators->findData("macro"));
    window.resize(1300, 760);
    window.show();
    QTest::qWait(20);
    QVERIFY(window.grab().save("toylisp-macro-evaluator-screenshot.png"));
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
    editor.runMacro(service, path);
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
    editor.runMacro(service, identity);
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
    editor.runMacro(service, path);
    QVERIFY(!editor.macroIsRunning());
    QCOMPARE(errors.size(), 0);
    editor.selectAll();
    editor.runMacro(service, path);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.toPlainText(), QString("unchanged\n"));
    QCOMPARE(
        editor.textCursor().selectedText(), QString("unchanged") + QChar(QChar::ParagraphSeparator));
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());

    const QString softLineBreak = QString("first") + QChar(QChar::LineSeparator) + "second";
    editor.setPlainText(softLineBreak);
    editor.selectAll();
    editor.runMacro(service, path);
    QTRY_VERIFY(!editor.macroIsRunning());
    QCOMPARE(editor.textCursor().selectedText(), softLineBreak);
    QVERIFY(!editor.document()->isModified());
    QVERIFY(!editor.document()->isUndoAvailable());

    const QString check = writeMacro(
        directory.path(),
        "linebreaks",
        "def macro(txt):\n    assert txt == 'first\\nsecond'\n    return 'normalized'\n");
    editor.runMacro(service, check);
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
    service.setInterpreter(executable);
    editor.runMacro(service, path);
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
    service.setInterpreter(python);
    editor.runMacro(service, path);
    QTRY_COMPARE(editor.toPlainText(), QString("recovered"));
}

void MacroTests::timeout()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(), "slow", "import time\ndef macro(txt): time.sleep(2); return txt\n");
    PythonMacroService limitedService({}, 50);
    limitedService.setInterpreter(python);
    PythonMacroJob job(limitedService);
    PythonMacroJob queued(limitedService);
    QSignalSpy completion(&job, &PythonMacroJob::completed);
    QSignalSpy queuedCompletion(&queued, &PythonMacroJob::completed);
    job.start(path, "original");
    queued.start(path, "queued");
    QTRY_COMPARE(completion.size(), 1);
    QCOMPARE(queuedCompletion.size(), 1);
    QVERIFY(completion.first().at(1).toString().contains("timed out"));
    QVERIFY(queuedCompletion.first().at(1).toString().contains("timed out"));

    const QString fast
        = writeMacro(directory.path(), "fast", "def macro(txt): return 'recovered'\n");
    PythonMacroJob recovered(limitedService);
    QSignalSpy recovery(&recovered, &PythonMacroJob::completed);
    recovered.start(fast, {});
    QTRY_COMPARE(recovery.size(), 1);
    QCOMPARE(recovery.first().at(0).toString(), QString("recovered"));
    QVERIFY(recovery.first().at(1).toString().isEmpty());
}

void MacroTests::sharedRuntimeAndReload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString runtime
        = writeMacro(directory.path(), "macro_runtime", "count = 0\nlabel = 'old'\n");
    QDir root(directory.path());
    QVERIFY(root.mkdir("python"));
    const QString macroDirectory = root.filePath("python");
    const QByteArray code
        = "import macro_runtime as runtime\nimport os, sys\nlocal = 0\n"
          "def macro(txt):\n    global local\n    local += 1\n    runtime.count += 1\n"
          "    return f'{os.getpid()}:{runtime.count}:{local}:{runtime.label}:A:{len(sys.path)}'\n";
    const QString path = writeMacro(macroDirectory, "test", code);
    MacroSelector selector(macroDirectory);
    QCOMPARE(selector.count(), 1);
    QCOMPARE(selector.currentText(), QString("test"));
    PythonMacroService shared(directory.path());
    shared.setInterpreter(python);
    EvaluatorRegistry registry;
    Editor first(registry), second(registry);
    first.setPlainText("input");
    first.selectAll();
    first.runMacro(shared, path);
    QTRY_VERIFY(!first.macroIsRunning());
    const QStringList initial = first.toPlainText().split(':');
    QCOMPARE(initial.size(), 6);
    QCOMPARE(initial.mid(1, 4), QStringList({"1", "1", "old", "A"}));

    QByteArray changed = code;
    changed.replace(":A:", ":B:");
    writeMacro(macroDirectory, "test", changed);
    writeMacro(directory.path(), "macro_runtime", "count = 0\nlabel = 'new'\n");
    second.setPlainText("input");
    second.selectAll();
    second.runMacro(shared, path);
    QTRY_VERIFY(!second.macroIsRunning());
    const QStringList reused = second.toPlainText().split(':');
    QCOMPARE(reused.size(), 6);
    QCOMPARE(reused.first(), initial.first());
    QCOMPARE(reused.mid(1, 4), QStringList({"2", "1", "old", "B"}));
    QCOMPARE(reused.last(), initial.last());

    shared.restart();
    first.selectAll();
    first.runMacro(shared, path);
    QTRY_VERIFY(!first.macroIsRunning());
    const QStringList restarted = first.toPlainText().split(':');
    QCOMPARE(restarted.size(), 6);
    QVERIFY(restarted.first() != initial.first());
    QCOMPARE(restarted.mid(1, 4), QStringList({"1", "1", "new", "B"}));
    QCOMPARE(restarted.last(), initial.last());
    QVERIFY(QFile::exists(runtime));
}

void MacroTests::softCancellationAndQueue()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeMacro(directory.path(), "macro_runtime", "events = []\n");
    const QString slow = writeMacro(
        directory.path(),
        "slow",
        "import os, time\nfrom pathlib import Path\nfrom macro_runtime import events\n"
        "def macro(txt):\n    events.append('start')\n"
        "    Path(__file__).with_suffix('.started').write_text(str(os.getpid()))\n"
        "    time.sleep(0.3)\n    events.append('finish')\n    return 'discarded'\n");
    const QString fast = writeMacro(
        directory.path(),
        "fast",
        "import os\nfrom macro_runtime import events\n"
        "def macro(txt):\n    events.append(txt)\n    return f'{os.getpid()}:' + "
        "','.join(events)\n");
    PythonMacroService shared(directory.path());
    shared.setInterpreter(python);
    PythonMacroJob active(shared), cancelled(shared), next(shared);
    QSignalSpy activeCompletion(&active, &PythonMacroJob::completed);
    QSignalSpy cancelledCompletion(&cancelled, &PythonMacroJob::completed);
    QSignalSpy completion(&next, &PythonMacroJob::completed);
    active.start(slow, {});
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.started"));
    QFile marker(directory.path() + "/slow.started");
    QVERIFY(marker.open(QIODevice::ReadOnly));
    const QString pid = QString::fromUtf8(marker.readAll());
    cancelled.start(fast, "cancelled");
    auto *destroyed = new PythonMacroJob(shared);
    destroyed->start(fast, "destroyed");
    next.start(fast, "next");
    active.cancel();
    cancelled.cancel();
    delete destroyed;
    QCOMPARE(completion.size(), 0);
    QTRY_COMPARE(completion.size(), 1);
    QVERIFY(completion.first().at(1).toString().isEmpty());
    QCOMPARE(completion.first().at(0).toString(), pid + ":start,finish,next");
    QCOMPARE(activeCompletion.size(), 0);
    QCOMPARE(cancelledCompletion.size(), 0);
}

void MacroTests::restartDiscardsRequests()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QDir root(directory.path());
    QVERIFY(root.mkdir("python"));
    writeMacro(directory.path(), "macro_runtime", "count = 0\n");
    writeMacro(
        root.filePath("python"),
        "test",
        "import os, time\nfrom pathlib import Path\nimport macro_runtime as runtime\n"
        "def macro(txt):\n    runtime.count += 1\n    if txt == 'hold':\n"
        "        Path(__file__).with_suffix('.started').touch()\n        time.sleep(2)\n"
        "    return f'{os.getpid()}|{runtime.count}'\n");
    QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/pythonInterpreter", python);
    MainWindow window(root.filePath("python"), directory.path());
    auto *first = window.findChild<Editor *>();
    auto *tabs = qobject_cast<QTabWidget *>(first->parentWidget()->parentWidget());
    auto *run = window.findChild<QAction *>("runMacroAction");
    auto *restart = window.findChild<QAction *>("restartMacroInterpreterAction");
    QVERIFY(restart);
    auto *menu = qobject_cast<QMenu *>(restart->parent());
    QVERIFY(menu);
    QCOMPARE(menu->title(), QString("&Actions"));
    first->setPlainText("seed");
    first->selectAll();
    run->trigger();
    QTRY_VERIFY(!first->macroIsRunning());
    const QString pid = first->toPlainText().section('|', 0, 0);
    QVERIFY(first->toPlainText().endsWith("|1"));
    first->setPlainText("hold");
    first->selectAll();
    run->trigger();
    QTRY_VERIFY(QFile::exists(root.filePath("python/test.started")));
    for (auto *action : window.findChildren<QAction *>()) {
        if (action->shortcut() == QKeySequence::New)
            action->trigger();
    }
    auto *second = qobject_cast<Editor *>(tabs->currentWidget());
    QVERIFY(second != first);
    second->setPlainText("queued");
    second->selectAll();
    run->trigger();
    QCOMPARE(window.findChild<QLabel *>("macroStatus")->text(), QString("Pending macros: 2"));
    writeMacro(directory.path(), "macro_runtime", "count = 10\n");
    restart->trigger();
    QVERIFY(!first->macroIsRunning());
    QVERIFY(!second->macroIsRunning());
    QCOMPARE(first->toPlainText(), QString("hold"));
    QCOMPARE(second->toPlainText(), QString("queued"));
    QVERIFY(!first->document()->isModified());
    QVERIFY(!second->document()->isModified());
    run->trigger();
    QTRY_VERIFY(!second->macroIsRunning());
    QVERIFY(second->toPlainText().endsWith("|11"));
    QVERIFY(second->toPlainText().section('|', 0, 0) != pid);
    QCOMPARE(first->toPlainText(), QString("hold"));
}

void MacroTests::failuresRetainRuntime()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    writeMacro(directory.path(), "macro_runtime", "count = 0\n");
    const QString path = writeMacro(
        directory.path(),
        "count",
        "import os\nimport macro_runtime as runtime\ndef macro(txt):\n"
        "    runtime.count += 1\n    if txt == 'error': raise ValueError('expected')\n"
        "    if txt == 'crash': os._exit(7)\n"
        "    return f'{os.getpid()}|{runtime.count}'\n");
    PythonMacroService shared(directory.path());
    shared.setInterpreter(python);
    PythonMacroJob first(shared), failure(shared), following(shared), crash(shared), queued(shared);
    QSignalSpy firstResult(&first, &PythonMacroJob::completed);
    QSignalSpy failureResult(&failure, &PythonMacroJob::completed);
    QSignalSpy followingResult(&following, &PythonMacroJob::completed);
    QSignalSpy crashResult(&crash, &PythonMacroJob::completed);
    QSignalSpy queuedResult(&queued, &PythonMacroJob::completed);
    first.start(path, {});
    QTRY_COMPARE(firstResult.size(), 1);
    const QString pid = firstResult.first().at(0).toString().section('|', 0, 0);
    failure.start(path, "error");
    following.start(path, {});
    QTRY_COMPARE(followingResult.size(), 1);
    QCOMPARE(failureResult.size(), 1);
    QVERIFY(failureResult.first().at(1).toString().contains("expected"));
    QCOMPARE(followingResult.first().at(0).toString(), pid + "|3");
    crash.start(path, "crash");
    queued.start(path, {});
    QTRY_COMPARE(crashResult.size(), 1);
    QCOMPARE(queuedResult.size(), 1);
    QVERIFY(crashResult.first().at(1).toString().contains("Process exited"));
    QVERIFY(!queuedResult.first().at(1).toString().isEmpty());
    PythonMacroJob recovered(shared);
    QSignalSpy recovery(&recovered, &PythonMacroJob::completed);
    recovered.start(path, {});
    QTRY_COMPARE(recovery.size(), 1);
    const QString fresh = recovery.first().at(0).toString();
    QVERIFY(fresh.endsWith("|1"));
    QVERIFY(fresh.section('|', 0, 0) != pid);
    shared.setInterpreter({});
    shared.setInterpreter(python);
    PythonMacroJob reconfigured(shared);
    QSignalSpy reconfiguration(&reconfigured, &PythonMacroJob::completed);
    reconfigured.start(path, {});
    QTRY_COMPARE(reconfiguration.size(), 1);
    QVERIFY(reconfiguration.first().at(0).toString().endsWith("|1"));
    QVERIFY(reconfiguration.first().at(0).toString().section('|', 0, 0) != fresh.section('|', 0, 0));
}

void MacroTests::queueLimit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeMacro(
        directory.path(),
        "slow",
        "import time\nfrom pathlib import Path\ndef macro(txt):\n"
        "    Path(__file__).with_suffix('.started').touch()\n    time.sleep(2)\n    return txt\n");
    PythonMacroJob active(service);
    active.start(path, {});
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.started"));
    QList<PythonMacroJob *> waiting;
    for (int i = 0; i < 16; ++i) {
        auto *job = new PythonMacroJob(service, &service);
        waiting.append(job);
        job->start(path, {});
    }
    PythonMacroJob rejected(service);
    QSignalSpy rejection(&rejected, &PythonMacroJob::completed);
    rejected.start(path, {});
    QCOMPARE(rejection.size(), 1);
    QVERIFY(rejection.first().at(1).toString().contains("queue is full"));
    qDeleteAll(waiting);
    active.cancel();
    service.restart();
}

void MacroTests::runtimeGeminiExample()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = QFINDTESTDATA("../src/macro/macro_runtime.py");
    QVERIFY(QFile::copy(source, directory.path() + "/macro_runtime.py"));
    QDir root(directory.path());
    QVERIFY(root.mkdir("google"));
    writeMacro(root.filePath("google"), "__init__", "");
    writeMacro(
        directory.path(), "ollama", "class Client:\n    def __init__(self, **kwargs): pass\n");
    writeMacro(
        root.filePath("google"),
        "genai",
        "created = 0\nclass Client:\n    def __init__(self):\n"
        "        global created\n        created += 1\n        self.number = created\n"
        "        self.calls = 0\n");
    const QString path = writeMacro(
        directory.path(),
        "gemini",
        "from macro_runtime import client\nimport os\ndef macro(txt):\n"
        "    client.calls += 1\n"
        "    return f'{os.getpid()}:{id(client)}:{client.number}:{client.calls}'\n");
    PythonMacroService shared(directory.path());
    shared.setInterpreter(python);
    PythonMacroJob first(shared), second(shared);
    QSignalSpy initial(&first, &PythonMacroJob::completed);
    QSignalSpy reused(&second, &PythonMacroJob::completed);
    first.start(path, {});
    second.start(path, {});
    QTRY_COMPARE(reused.size(), 1);
    QCOMPARE(initial.size(), 1);
    QVERIFY(initial.first().at(1).toString().isEmpty());
    QVERIFY(reused.first().at(1).toString().isEmpty());
    const QStringList firstValues = initial.first().at(0).toString().split(':');
    const QStringList secondValues = reused.first().at(0).toString().split(':');
    QCOMPARE(firstValues.size(), 4);
    QCOMPARE(secondValues.mid(0, 3), firstValues.mid(0, 3));
    QCOMPARE(firstValues.mid(2), QStringList({"1", "1"}));
    QCOMPARE(secondValues.mid(2), QStringList({"1", "2"}));

    shared.restart();
    writeMacro(root.filePath("google"), "genai", "raise RuntimeError('SDK unavailable')\n");
    PythonMacroJob unavailable(shared);
    QSignalSpy missing(&unavailable, &PythonMacroJob::completed);
    unavailable.start(path, {});
    QTRY_COMPARE(missing.size(), 1);
    QVERIFY(missing.first().at(1).toString().contains("SDK unavailable"));
    const QString local
        = writeMacro(directory.path(), "local", "def macro(txt): return 'local works'\n");
    PythonMacroJob unrelated(shared);
    QSignalSpy localResult(&unrelated, &PythonMacroJob::completed);
    unrelated.start(local, {});
    QTRY_COMPARE(localResult.size(), 1);
    QCOMPARE(localResult.first().at(0).toString(), QString("local works"));
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
    editor.runMacro(service, path);
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
    editor.runMacro(service, path);
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
    editor.runMacro(service, path);
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.started"));
    select(editor, start, end);
    editor.insertPlainText("x");
    QVERIFY(!editor.macroIsRunning());
    if (undo)
        editor.undo();
    const QString edited = editor.toPlainText();
    QTRY_VERIFY(QFile::exists(directory.path() + "/slow.finished"));
    QCOMPARE(editor.toPlainText(), edited);
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
    service.setInterpreter(executable);
    PythonMacroJob job(service);
    QSignalSpy completion(&job, &PythonMacroJob::completed);
    job.start(path, input);
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
    auto *restart = window.findChild<QAction *>("restartMacroInterpreterAction");
    QVERIFY(restart);
    auto *menu = qobject_cast<QMenu *>(restart->parent());
    QVERIFY(menu);
    menu->popup(window.menuBar()->mapToGlobal(
        window.menuBar()->actionGeometry(menu->menuAction()).bottomLeft()));
    QTest::qWait(30);
    QVERIFY(menu->isVisible());
    QVERIFY(menu->grab().save("toylisp-macro-actions-screenshot.png"));
    menu->hide();
    editor->document()->setModified(false);
}

QTEST_MAIN(MacroTests)
