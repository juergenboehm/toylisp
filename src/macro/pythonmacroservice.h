#pragma once

#include "processes/jsonprocess.h"

// One serial macro interpreter shared by the application's editors.
class PythonMacroService : public QObject
{
public:
    explicit PythonMacroService(const QString &runtimeDirectory = {}, int timeoutMs = 40000);

    ~PythonMacroService() override;

    void setInterpreter(const QString &interpreter);

    void restart();

    void call(
        const QString &path,
        const QString &text,
        QObject *context,
        JsonProcess::Completion completion);

    void cancel(QObject *context);

private:
    JsonProcess worker;
    QString runtimeDirectory;
    QString interpreter;
    QString dispatcher;
};
