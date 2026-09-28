#include "editor/blocks.h"
#include "evaluators/evaluator.h"
#include "ui/editor.h"
#include "ui/mainwindow.h"
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QtTest>

class Tests : public QObject
{
    Q_OBJECT
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
private slots:
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
    }
    void blocks()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        EvaluatorRegistry registry;
        Editor editor(registry);
        editor.setPlainText(input);
        evaluate(editor);
        QCOMPARE(editor.toPlainText(), expected);
        editor.undo();
        QCOMPARE(editor.toPlainText(), input);
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
        QVERIFY(!locateBlock(">>\n1\n ==", 5).valid);
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
