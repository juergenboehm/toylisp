#include "pythonmacrojob.h"
#include "pythonmacroservice.h"

PythonMacroJob::PythonMacroJob(PythonMacroService &service, QObject *parent)
    : QObject(parent)
    , service(&service)
{}

PythonMacroJob::~PythonMacroJob()
{
    cancel();
}

void PythonMacroJob::start(const QString &path, const QString &text)
{
    if (started || cancelled)
        return;
    started = true;

    if (!service) {
        emit completed({}, "Macro service is no longer available");
        return;
    }

    service->call(path, text, this, [this](const QJsonObject &result, const QString &error) {
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
    if (service)
        service->cancel(this);
}
