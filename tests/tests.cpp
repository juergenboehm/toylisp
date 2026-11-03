#include "editor/blocks.h"
#include "evaluators/arith/arithmeticevaluator.h"
#include "evaluators/evaluator.h"
#include "ui/editor.h"
#include "ui/mainwindow.h"
#include <QAction>
#include <QFile>
#include <QFontMetricsF>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QWheelEvent>
#include <QtTest>

class Tests : public QObject
{
    Q_OBJECT
    static constexpr qreal normalPointSize = 12.0;
    static constexpr qreal oneNotchPointSize = 12.6;

    QTemporaryDir settingsDirectory;
    static void evaluate(Editor &editor, int occurrence = 0)
    {
        int position = -1;
        for (int i = 0; i <= occurrence; ++i)
            position = editor.toPlainText().indexOf("==", position + 1);
        auto cursor = editor.textCursor();
        cursor.setPosition(position);
        cursor.movePosition(QTextCursor::EndOfBlock);
        editor.setTextCursor(cursor);
        QTest::keyClick(&editor, Qt::Key_Return);
    }
    static bool wheel(
        Editor &editor,
        QPoint angleDelta,
        Qt::KeyboardModifiers modifiers = Qt::ControlModifier,
        QPoint pixelDelta = {})
    {
        const auto position = editor.viewport()->rect().center();
        QWheelEvent event(
            position,
            editor.viewport()->mapToGlobal(position),
            pixelDelta,
            angleDelta,
            Qt::NoButton,
            modifiers,
            Qt::NoScrollPhase,
            false);
        event.setAccepted(false);
        QApplication::sendEvent(editor.viewport(), &event);
        return event.isAccepted();
    }
private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void arithmetic_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("precedence") << "2+3*4" << "14";
        QTest::newRow("parentheses") << "(2+3)*4" << "20";
        QTest::newRow("unary") << "-(2+3)*-2" << "10";
        QTest::newRow("division-associativity") << "12/3/2" << "2";
        QTest::newRow("decimal") << ".5 + 1.25" << "1.75";
        QTest::newRow("assignment") << "x = 3\n\nx * (x + 1)" << "12";
        QTest::newRow("empty") << "\n  " << "";
    }
    void arithmetic()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        ArithmeticEvaluator evaluator;
        auto result = evaluator.evaluate(input);
        QVERIFY2(result.ok, qPrintable(result.text));
        QCOMPARE(result.text, expected);
    }
    void malformed_data()
    {
        QTest::addColumn<QString>("input");
        for (const auto &input :
             {"1/0",
              "missing",
              "(1+2",
              "1 2",
              "1=3",
              "x=",
              "x==3",
              "2**3",
              "1e999",
              "1e308*1e308",
              "2+"})
            QTest::newRow(input) << QString(input);
    }
    void malformed()
    {
        QFETCH(QString, input);
        ArithmeticEvaluator evaluator;
        auto result = evaluator.evaluate("\n" + input);
        QVERIFY(!result.ok);
        QVERIFY(result.text.startsWith("Error in line 2:"));
    }
    void transactionalEnvironment()
    {
        ArithmeticEvaluator first, second;
        QVERIFY(first.evaluate("x=7").ok);
        QVERIFY(!first.evaluate("x=9\ny=2\n1/0").ok);
        QCOMPARE(first.evaluate("x").text, QString("7"));
        QVERIFY(!first.evaluate("y").ok);
        QVERIFY(!second.evaluate("x").ok);
        QString deep(300, '(');
        QVERIFY(!first.evaluate(deep + "1" + QString(300, ')')).ok);
    }
    void blocks_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("first") << ">>\n2+3\n==" << ">>\n2+3\n==\n5\n##";
        QTest::newRow("replace-multiline")
            << ">>\n2+3\n==\nold\nresult\n##\ntail" << ">>\n2+3\n==\n5\n##\ntail";
        QTest::newRow("missing-end") << ">>\n2+3\n==\nkeep" << ">>\n2+3\n==\n5\n##\nkeep";
        QTest::newRow("next-block-boundary")
            << ">>\n2+3\n==\nold\n>>\n7\n==\n7\n##" << ">>\n2+3\n==\n5\n##\nold\n>>\n7\n==\n7\n##";
        QTest::newRow("explicit") << ">>\n2+3\n==arith" << ">>\n2+3\n==arith\n5\n##";
        QTest::newRow("empty") << ">>\n==" << ">>\n==\n##";
        QTest::newRow("unicode-surroundings") << QString::fromUtf8("😀\n>>\n2+3\n==\n😀\n##\n尾")
                                              << QString::fromUtf8("😀\n>>\n2+3\n==\n5\n##\n尾");
        QTest::newRow("blank-input-lines") << ">>\n\n2+3\n\n==\nold\n##\n"
                                           << ">>\n\n2+3\n\n==\n5\n##\n";
        QTest::newRow("nearest-start")
            << ">>\nignored\n>>\n2+3\n==" << ">>\nignored\n>>\n2+3\n==\n5\n##";
        QTest::newRow("soft-input-lines")
            << ">>\nx\u00a0=\u00a03\u2028x+1\n==" << ">>\nx = 3\nx+1\n==\n4\n##";
        QTest::newRow("soft-marker-boundaries")
            << "prefix\u2028>>\u20282+3\n==arith\u2028old\u2028##\u2028tail"
            << "prefix\n>>\n2+3\n==arith\n5\n##\ntail";
    }
    void blocks()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText(input);
        const QString original = editor.document()->toRawText();
        evaluate(editor);
        QCOMPARE(editor.toPlainText(), expected);
        editor.undo();
        QCOMPARE(editor.document()->toRawText(), original);
        editor.redo();
        QCOMPARE(editor.toPlainText(), expected);
        evaluate(editor);
        QCOMPARE(editor.toPlainText(), expected);
    }
    void missingStartAndErrors()
    {
        EvaluatorRegistry registry;
        Editor editor(registry);
        QSignalSpy messages(&editor, &Editor::diagnostic);
        editor.setPlainText("1\n==");
        evaluate(editor);
        QCOMPARE(editor.toPlainText(), QString("1\n=="));
        QCOMPARE(messages.count(), 1);
        editor.setPlainText(">>\n1\n==lisp");
        evaluate(editor);
        QVERIFY(editor.toPlainText().contains("Unknown evaluator: lisp\n##"));
        editor.setPlainText(">>\n1/0\n==");
        evaluate(editor);
        QVERIFY(editor.toPlainText().contains("Error in line 1: Division by zero\n##"));
        editor.setPlainText(">>\n1\n ==");
        QVERIFY(!locateBlock(editor.document()->lastBlock()).valid);
    }
    void sharedSessionSurvivesEditor()
    {
        EvaluatorRegistry registry;
        {
            Editor first(registry);
            first.setPlainText(">>\nx=17\n==");
            evaluate(first);
        }
        Editor second(registry);
        second.setPlainText(">>\nx*2\n==arith");
        evaluate(second);
        QVERIFY(second.toPlainText().endsWith("\n34\n##"));
        EvaluatorRegistry independent;
        QVERIFY(!independent.evaluate("arith", "x").ok);
    }
    void textZoomWheel()
    {
        EvaluatorRegistry registry;
        Editor editor(registry);
        const auto family = editor.font().family();
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);
        QVERIFY(wheel(editor, {0, 120}));
        QCOMPARE(editor.font().pointSizeF(), oneNotchPointSize);
        QCOMPARE(editor.font().family(), family);
        QCOMPARE(editor.tabStopDistance(), QFontMetricsF(editor.font()).horizontalAdvance(' ') * 4);
        QVERIFY(wheel(editor, {0, -120}));
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);

        // Two half-notches must have the same effect as one complete notch.
        wheel(editor, {0, 60});
        QVERIFY(editor.font().pointSizeF() > normalPointSize);
        QVERIFY(editor.font().pointSizeF() < oneNotchPointSize);
        wheel(editor, {0, 60});
        QCOMPARE(editor.font().pointSizeF(), oneNotchPointSize);
        editor.resetTextSize();
        wheel(editor, {}, Qt::ControlModifier, {0, 10});
        QVERIFY(editor.font().pointSizeF() > normalPointSize);
        QVERIFY(editor.font().pointSizeF() < oneNotchPointSize);
        wheel(editor, {}, Qt::ControlModifier, {0, -10});
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);

        QVERIFY(wheel(editor, {}));
        QVERIFY(wheel(editor, {120, 0}));
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);
    }
    void textZoomLimitsAndScrolling()
    {
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.resize(400, 200);
        editor.setPlainText(QString("line\n").repeated(300));
        editor.show();
        QCoreApplication::processEvents();
        auto *scrollBar = editor.verticalScrollBar();
        QVERIFY(scrollBar->maximum() > 0);
        QCOMPARE(scrollBar->value(), 0);
        wheel(editor, {0, -120}, Qt::NoModifier);
        QVERIFY(scrollBar->value() > 0);
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);
        wheel(editor, {0, 120}, Qt::ShiftModifier);
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);

        wheel(editor, {0, 120000});
        QCOMPARE(editor.font().pointSizeF(), 96.0);
        wheel(editor, {0, 120});
        QCOMPARE(editor.font().pointSizeF(), 96.0);
        wheel(editor, {0, -120});
        QVERIFY(editor.font().pointSizeF() < 96.0);
        wheel(editor, {0, -120000});
        QCOMPARE(editor.font().pointSizeF(), 1.0);
        wheel(editor, {0, -120});
        QCOMPARE(editor.font().pointSizeF(), 1.0);
        wheel(editor, {0, 120});
        QCOMPARE(editor.font().pointSizeF(), 1.05);
        editor.resetTextSize();
        QCOMPARE(editor.font().pointSizeF(), normalPointSize);
    }
    void textZoomPreservesEditingState()
    {
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText("(\tvalue)\nsecond line");
        const auto original = editor.document()->toRawText();
        auto cursor = editor.textCursor();
        cursor.setPosition(2);
        cursor.setPosition(7, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);
        wheel(editor, {0, 120});
        editor.resetTextSize();
        QCOMPARE(editor.document()->toRawText(), original);
        QCOMPARE(editor.textCursor().position(), cursor.position());
        QCOMPARE(editor.textCursor().anchor(), cursor.anchor());
        QVERIFY(!editor.document()->isModified());
        QVERIFY(!editor.document()->isUndoAvailable());

        editor.insertPlainText("replacement");
        const auto edited = editor.document()->toRawText();
        wheel(editor, {0, 120});
        editor.resetTextSize();
        QCOMPARE(editor.document()->toRawText(), edited);
        QVERIFY(editor.document()->isModified());
        editor.undo();
        QCOMPARE(editor.document()->toRawText(), original);
        QVERIFY(!editor.document()->isModified());
        wheel(editor, {0, 120});
        QVERIFY(editor.document()->isRedoAvailable());
        editor.redo();
        QCOMPARE(editor.document()->toRawText(), edited);
        QCOMPARE(editor.font().pointSizeF(), oneNotchPointSize);
    }
    void textZoomPerTabAndResetAction()
    {
        MainWindow window;
        window.show();
        window.activateWindow();
        auto *first = window.findChild<Editor *>();
        QVERIFY(first);
        auto *tabs = qobject_cast<QTabWidget *>(first->parentWidget()->parentWidget());
        QVERIFY(tabs);
        auto *normalSize = window.findChild<QAction *>("normalSizeAction");
        QVERIFY(normalSize);
        QCOMPARE(window.menuBar()->actions().at(2)->text(), QString("&View"));
        QCOMPARE(normalSize->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_0));
        wheel(*first, {0, 120});
        auto *toolbar = window.findChild<QToolBar *>();
        QVERIFY(toolbar);
        toolbar->actions()[0]->trigger();
        auto *second = qobject_cast<Editor *>(tabs->currentWidget());
        QVERIFY(second && second != first);
        QCOMPARE(second->font().pointSizeF(), normalPointSize);
        wheel(*second, {0, 240});
        const auto secondSize = second->font().pointSizeF();
        tabs->setCurrentWidget(first);
        QCOMPARE(first->font().pointSizeF(), oneNotchPointSize);
        normalSize->trigger();
        QCOMPARE(first->font().pointSizeF(), normalPointSize);
        QCOMPARE(second->font().pointSizeF(), secondSize);

        wheel(*first, {0, 120});
        tabs->setCurrentWidget(second);
        second->setFocus();
        QTRY_VERIFY(second->hasFocus());
        QTest::keyClick(second, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(second->font().pointSizeF(), normalPointSize);
        QCOMPARE(first->font().pointSizeF(), oneNotchPointSize);
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1)));
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 0)));
        QCOMPARE(tabs->count(), 0);
        normalSize->trigger(); // Closing the last tab leaves the menu safe to use.
        toolbar->actions()[0]->trigger();
        QCOMPARE(qobject_cast<Editor *>(tabs->currentWidget())->font().pointSizeF(), normalPointSize);
    }
    void files()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        EvaluatorRegistry registry;
        Editor editor(registry);
        QString error, path = directory.filePath("test.txt");
        editor.insertPlainText("Grüße\n>>\nx=5\n==");
        QVERIFY(editor.document()->isModified());
        QVERIFY2(editor.save(path, error), qPrintable(error));
        QVERIFY(!editor.document()->isModified());
        Editor loaded(registry);
        QVERIFY(loaded.load(path, error));
        QCOMPARE(loaded.toPlainText(), editor.toPlainText());
        QVERIFY(!registry.evaluate("arith", "x").ok);
        loaded.insertPlainText("changed");
        QVERIFY(!loaded.save(directory.filePath("missing/file.txt"), error));
        QVERIFY(loaded.document()->isModified());
        QCOMPARE(loaded.filePath, path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("one\r\ntwo\r\n");
        file.close();
        QVERIFY(loaded.load(path, error));
        QVERIFY(loaded.crlf);
        QVERIFY(loaded.save(path, error));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("one\r\ntwo\r\n"));
        file.close();
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("\xff");
        file.close();
        QVERIFY(!loaded.load(path, error));
    }
    void closePromptsAndTabActions()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MainWindow window;
        window.show();
        auto *editor = window.findChild<Editor *>();
        auto *tabs = qobject_cast<QTabWidget *>(editor->parentWidget()->parentWidget());
        // QTabWidget owns the editor through its internal stacked widget.
        QVERIFY(tabs);
        editor->insertPlainText("saved text");
        QString error;
        QVERIFY(editor->save(directory.filePath("close.txt"), error));
        editor->insertPlainText(" and changes");
        auto answer = [](QMessageBox::StandardButton choice) {
            QTimer::singleShot(0, [choice] {
                auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (message)
                    message->button(choice)->click();
            });
        };
        answer(QMessageBox::Cancel);
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        answer(QMessageBox::Save);
        QVERIFY(window.close());
        QFile file(editor->filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(file.readAll()), editor->toPlainText());
        file.close();
        window.show();
        auto *toolbar = window.findChild<QToolBar *>();
        toolbar->actions()[0]->trigger();
        QCOMPARE(tabs->count(), 2);
        auto *second = qobject_cast<Editor *>(tabs->currentWidget());
        second->insertPlainText("discard this");
        answer(QMessageBox::Discard);
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabCloseRequested", Q_ARG(int, 1)));
        QCOMPARE(tabs->count(), 1);
        QCOMPARE(tabs->currentWidget(), editor);
    }
    void windowAndScreenshot()
    {
        MainWindow window;
        window.show();
        QTest::qWait(50);
        QCOMPARE(window.findChildren<QSplitter *>().size(), 2);
        QCOMPARE(window.findChildren<QTabWidget *>().size(), 3);
        auto *toolbar = window.findChild<QToolBar *>();
        QVERIFY(toolbar);
        for (int i = 0; i < 3; ++i)
            QVERIFY(!toolbar->actions()[i]->icon().pixmap(24).isNull());
        auto *editor = window.findChild<Editor *>();
        QVERIFY(editor);
        editor->setPlainText(">>\nx = 3\nx * (x + 1)\n==");
        evaluate(*editor);
        QVERIFY(window.grab().save("toylisp-screenshot.png"));
        editor->document()->setModified(false);
    }
};
QTEST_MAIN(Tests)
#include "tests.moc"
