#include "pythonmacrojob.h"
#include <QFile>
#include <QFileInfo>

PythonMacroJob::PythonMacroJob(QObject *parent, int timeoutMs)
    : QObject(parent)
    , worker(timeoutMs)
{}

void PythonMacroJob::start(const QString &interpreter, const QString &path, const QString &text)
{
    if (started || cancelled)
        return;
    started = true;

    if (interpreter.isEmpty()) {
        emit completed({}, "Set a Python interpreter under Sanskrit → Python Interpreter…");
        return;
    }

    Q_INIT_RESOURCE(macro_python);
    QFile dispatcher(":/macro/dispatcher.py");
    if (!dispatcher.open(QIODevice::ReadOnly)) {
        emit completed({}, "Cannot read the bundled macro dispatcher");
        return;
    }

    worker.configure(
        interpreter, {"-I", "-u", "-X", "utf8", "-c", QString::fromUtf8(dispatcher.readAll())});
    worker.call(
        "macro.run",
        {{"path", QFileInfo(path).absoluteFilePath()}, {"text", text}},
        this,
        [this](const QJsonObject &result, const QString &error) {
            if (cancelled)
                return;
            if (!error.isEmpty())
                emit completed({}, error);
            else if (!result.value("text").isString())
                emit completed({}, "Invalid macro response: expected a string");
            else
                emit completed(result.value("text").toString(), {});
        });
}

void PythonMacroJob::cancel()
{
    cancelled = true;
    worker.cancel(this);
    worker.configure({}, {});
}
