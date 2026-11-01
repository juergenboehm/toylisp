#include "mainwindow.h"
#include "editor.h"
#include "evaluators/sanskrit/sanskritevaluator.h"
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow()
{
    Q_INIT_RESOURCE(icons);
    setWindowTitle("ToyLisp");
    resize(1100, 760);
    auto *vertical = new QSplitter(Qt::Vertical, this);
    auto *horizontal = new QSplitter(Qt::Horizontal, vertical);
    editors = new QTabWidget(horizontal);
    editors->setTabsClosable(true);
    editors->setMovable(true);
    editors->setDocumentMode(true);
    auto *auxiliary = new QTabWidget(horizontal);
    auto *placeholder = new QLabel("Auxiliary views", auxiliary);
    placeholder->setAlignment(Qt::AlignCenter);
    auxiliary->addTab(placeholder, "Tools");
    auto *bottom = new QTabWidget(vertical);
    output = new QPlainTextEdit(bottom);
    output->setReadOnly(true);
    output->setMaximumBlockCount(2000);
    bottom->addTab(output, "Output");
    horizontal->setSizes({850, 250});
    vertical->setSizes({550, 150});
    horizontal->setStretchFactor(0, 1);
    vertical->setStretchFactor(0, 1);
    setCentralWidget(vertical);
    connect(editors, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    auto *file = menuBar()->addMenu("&File");
    auto *edit = menuBar()->addMenu("&Edit");
    auto *view = menuBar()->addMenu("&View");
    auto *normalSize = view->addAction("&Normal Size");
    normalSize->setObjectName("normalSizeAction");
    normalSize->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(normalSize, &QAction::triggered, this, [this] {
        if (auto *editor = currentEditor())
            editor->resetTextSize();
    });
    auto sanskrit = std::make_unique<SanskritEvaluator>();
    auto *sanskritEvaluator = sanskrit.get();
    registry.addEvaluator("sanskrit", std::move(sanskrit));
    auto *sanskritMenu = menuBar()->addMenu("&Sanskrit");
    auto *languages = sanskritMenu->addMenu("Result &Language");
    auto *languageGroup = new QActionGroup(this);
    const QString savedLanguage
        = QSettings("ToyLisp", "ToyLisp").value("sanskrit/resultLanguage", "de").toString();
    for (const auto &code : {QString("de"), QString("en")}) {
        const bool german = code == "de";
        auto *action = languages->addAction(
            german ? "&German (Petersburg)" : "&English (Monier-Williams)");
        action->setObjectName("sanskritLanguage_" + code);
        action->setCheckable(true);
        languageGroup->addAction(action);
        const bool selected = code == savedLanguage || (german && savedLanguage != "en");
        action->setChecked(selected);
        if (selected)
            sanskritEvaluator->setLanguage(
                german ? SanskritLanguage::German : SanskritLanguage::English);
        connect(action, &QAction::triggered, this, [sanskritEvaluator, code, german] {
            sanskritEvaluator->setLanguage(
                german ? SanskritLanguage::German : SanskritLanguage::English);
            QSettings("ToyLisp", "ToyLisp").setValue("sanskrit/resultLanguage", code);
        });
    }
    auto *cancelLookup = sanskritMenu->addAction("&Cancel Pending Evaluations in This Tab");
    cancelLookup->setObjectName("cancelEvaluationsAction");
    cancelLookup->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancelLookup, &QAction::triggered, this, [this] {
        if (auto *editor = currentEditor())
            editor->cancelEvaluations();
    });
    auto *toolbar = addToolBar("File");
    toolbar->setObjectName("fileToolbar");
    auto addFileAction = [&](QString title, QString icon, QKeySequence key, auto function) {
        auto *action = file->addAction(QIcon(icon), title);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, function);
        toolbar->addAction(action);
    };
    addFileAction("&New", ":/icons/new.svg", QKeySequence::New, [this] { addEditor(); });
    addFileAction("&Open…", ":/icons/open.svg", QKeySequence::Open, [this] { openFile(); });
    addFileAction("&Save", ":/icons/save.svg", QKeySequence::Save, [this] {
        saveEditor(currentEditor());
    });
    auto *saveAs = file->addAction("Save &As…");
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs, &QAction::triggered, this, [this] { saveEditor(currentEditor(), true); });
    auto *close = file->addAction("&Close");
    close->setShortcut(QKeySequence::Close);
    connect(close, &QAction::triggered, this, [this] { closeTab(editors->currentIndex()); });
    file->addSeparator();
    auto *quit = file->addAction("&Quit");
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto addEditAction = [&](QString title, QKeySequence key, auto function) {
        auto *action = edit->addAction(title);
        action->setShortcut(key);
        connect(action, &QAction::triggered, this, [this, function] {
            if (auto *editor = currentEditor())
                (editor->*function)();
        });
    };
    addEditAction("&Undo", QKeySequence::Undo, &Editor::undo);
    addEditAction("&Redo", QKeySequence::Redo, &Editor::redo);
    edit->addSeparator();
    addEditAction("Cu&t", QKeySequence::Cut, &Editor::cut);
    addEditAction("&Copy", QKeySequence::Copy, &Editor::copy);
    addEditAction("&Paste", QKeySequence::Paste, &Editor::paste);
    addEditAction("Select &All", QKeySequence::SelectAll, &Editor::selectAll);
    auto *find = edit->addAction("&Find / Replace…");
    find->setShortcuts({QKeySequence::Find, QKeySequence::Replace});
    connect(find, &QAction::triggered, this, &MainWindow::findReplace);
    toolbar->addSeparator();
    toolbar->addWidget(new QLabel(" Default evaluator: ", toolbar));
    auto *selector = new QComboBox(toolbar);
    selector->addItem("Arithmetic", "arith");
    selector->addItem("Sanskrit Lookup", "sanskrit");
    selector->setObjectName("evaluatorSelector");
    toolbar->addWidget(selector);
    connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, selector] {
        registry.defaultName = selector->currentData().toString();
    });
    statusBar()->showMessage("Ready — enter >>, input, then == and Return to evaluate");
    lookupStatus = new QLabel(this);
    lookupStatus->setObjectName("lookupStatus");
    statusBar()->addPermanentWidget(lookupStatus);
    addEditor();
}
Editor *MainWindow::currentEditor() const
{
    return qobject_cast<Editor *>(editors->currentWidget());
}
Editor *MainWindow::addEditor()
{
    auto *editor = new Editor(registry, editors);
    editor->setProperty("untitledName", QString("Untitled %1").arg(++untitledCount));
    editors->addTab(editor, editor->property("untitledName").toString());
    editors->setCurrentWidget(editor);
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this, editor] {
        updateTitle(editor);
    });
    connect(editor, &Editor::diagnostic, this, [this, editor](const QString &message) {
        output->appendPlainText(editors->tabText(editors->indexOf(editor)) + ": " + message);
        statusBar()->showMessage(message, 10000);
    });
    connect(editor, &Editor::evaluationActivityChanged, this, &MainWindow::updateEvaluationActivity);
    editor->setFocus();
    return editor;
}
void MainWindow::updateEvaluationActivity()
{
    int count = 0;
    for (int i = 0; i < editors->count(); ++i)
        count += qobject_cast<Editor *>(editors->widget(i))->activeEvaluationCount();
    lookupStatus->setText(count ? QString("Pending lookups: %1").arg(count) : QString());
}
void MainWindow::updateTitle(Editor *editor)
{
    int index = editors->indexOf(editor);
    QString name = editor->filePath.isEmpty() ? editor->property("untitledName").toString()
                                              : QFileInfo(editor->filePath).fileName();
    editors->setTabText(index, name + (editor->document()->isModified() ? " *" : ""));
    editors->setTabToolTip(index, editor->filePath);
}
void MainWindow::openFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Open text file");
    if (path.isEmpty())
        return;
    for (int i = 0; i < editors->count(); ++i) {
        auto *editor = qobject_cast<Editor *>(editors->widget(i));
        if (QFileInfo(editor->filePath).canonicalFilePath() == QFileInfo(path).canonicalFilePath()) {
            editors->setCurrentIndex(i);
            return;
        }
    }
    auto *editor = addEditor();
    QString error;
    if (!editor->load(path, error)) {
        QMessageBox::warning(this, "Open failed", error);
        editors->removeTab(editors->indexOf(editor));
        editor->deleteLater();
        return;
    }
    updateTitle(editor);
}
bool MainWindow::saveEditor(Editor *editor, bool saveAs)
{
    if (!editor)
        return true;
    QString path = editor->filePath;
    if (path.isEmpty() || saveAs)
        path = QFileDialog::getSaveFileName(this, "Save text file", path);
    if (path.isEmpty())
        return false;
    QString error;
    if (!editor->save(path, error)) {
        QMessageBox::warning(this, "Save failed", error);
        return false;
    }
    updateTitle(editor);
    return true;
}
bool MainWindow::confirmClose(Editor *editor)
{
    if (!editor || !editor->document()->isModified())
        return true;
    auto answer = QMessageBox::question(
        this,
        "Unsaved changes",
        "Save changes to " + editors->tabText(editors->indexOf(editor)) + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveEditor(editor);
}
void MainWindow::closeTab(int index)
{
    auto *editor = qobject_cast<Editor *>(editors->widget(index));
    if (!editor || !confirmClose(editor))
        return;
    editor->cancelEvaluations();
    editors->removeTab(index);
    editor->deleteLater();
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    // Modal save prompts process events; no lookup may dirty a tab already checked.
    for (int i = 0; i < editors->count(); ++i)
        qobject_cast<Editor *>(editors->widget(i))->cancelEvaluations();
    for (int i = 0; i < editors->count(); ++i) {
        if (!confirmClose(qobject_cast<Editor *>(editors->widget(i)))) {
            event->ignore();
            return;
        }
    }
    event->accept();
}
void MainWindow::findReplace()
{
    auto *editor = currentEditor();
    if (!editor)
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Find / Replace");
    auto *layout = new QFormLayout(&dialog);
    QLineEdit search, replacement;
    QLabel feedback;
    QPushButton next("Find next"), replace("Replace"), all("Replace all");
    layout->addRow("Find (case-sensitive)", &search);
    layout->addRow("Replace with", &replacement);
    layout->addRow(&next, &replace);
    layout->addRow(&all);
    layout->addRow(&feedback);
    auto findNext = [&] {
        if (search.text().isEmpty())
            return;
        bool found = editor->find(search.text(), QTextDocument::FindCaseSensitively);
        if (!found) {
            auto original = editor->textCursor();
            editor->moveCursor(QTextCursor::Start);
            found = editor->find(search.text(), QTextDocument::FindCaseSensitively);
            if (!found)
                editor->setTextCursor(original);
        }
        feedback.setText(found ? "Match selected" : "No match");
    };
    connect(&next, &QPushButton::clicked, &dialog, findNext);
    connect(&replace, &QPushButton::clicked, &dialog, [&] {
        auto cursor = editor->textCursor();
        if (!search.text().isEmpty() && cursor.selectedText() == search.text())
            cursor.insertText(replacement.text());
        findNext();
    });
    connect(&all, &QPushButton::clicked, &dialog, [&] {
        if (search.text().isEmpty())
            return;
        auto group = editor->textCursor();
        group.beginEditBlock();
        QTextCursor cursor(editor->document());
        int count = 0;
        while (true) {
            cursor = editor->document()
                         ->find(search.text(), cursor, QTextDocument::FindCaseSensitively);
            if (cursor.isNull())
                break;
            cursor.insertText(replacement.text());
            ++count;
        }
        group.endEditBlock();
        feedback.setText(QString("Replaced %1 occurrences").arg(count));
    });
    dialog.exec();
}
