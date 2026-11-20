#pragma once

#include "processes/jsonprocess.h"

class PythonMacroJob : public QObject
{
    Q_OBJECT

public:
    explicit PythonMacroJob(QObject *parent = nullptr, int timeoutMs = 40000);

    void start(const QString &interpreter, const QString &path, const QString &text);

    void cancel();

signals:
    void completed(const QString &text, const QString &error);

private:
    JsonProcess worker;
    bool started = false;
    bool cancelled = false;
};
