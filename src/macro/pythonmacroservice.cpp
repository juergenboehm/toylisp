#include "pythonmacroservice.h"
#include <QFile>
#include <QFileInfo>

PythonMacroService::PythonMacroService(const QString &runtimeDirectory, int timeoutMs)
    : worker(timeoutMs)
    , runtimeDirectory(
          runtimeDirectory.isEmpty() ? QString::fromUtf8(TOYLISP_MACRO_RUNTIME_DIRECTORY)
                                     : runtimeDirectory)
{
    Q_INIT_RESOURCE(macro_python);
    QFile file(":/macro/dispatcher.py");
    if (file.open(QIODevice::ReadOnly))
        dispatcher = QString::fromUtf8(file.readAll());
}

PythonMacroService::~PythonMacroService()
{
    worker.reset("Macro service stopped");
}

void PythonMacroService::setInterpreter(const QString &path)
{
    interpreter = path;
    const QStringList arguments
        = path.isEmpty()
              ? QStringList()
              : QStringList{"-I", "-B", "-u", "-X", "utf8", "-c", dispatcher, runtimeDirectory};
    worker.configure(path, arguments, false);
}

void PythonMacroService::restart()
{
    worker.reset("Macro interpreter restarted");
}

void PythonMacroService::call(
    const QString &path, const QString &text, QObject *context, JsonProcess::Completion completion)
{
    if (interpreter.isEmpty()) {
        completion({}, "Set a Python interpreter under Sanskrit → Python Interpreter…");
        return;
    }
    if (dispatcher.isEmpty()) {
        completion({}, "Cannot read the bundled macro dispatcher");
        return;
    }
    worker.call(
        "macro.run",
        {{"path", QFileInfo(path).absoluteFilePath()}, {"text", text}},
        context,
        std::move(completion));
}

void PythonMacroService::cancel(QObject *context)
{
    worker.cancel(context);
}
