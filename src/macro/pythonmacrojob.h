#pragma once

#include <QObject>
#include <QPointer>

class PythonMacroService;

class PythonMacroJob : public QObject
{
    Q_OBJECT

public:
    explicit PythonMacroJob(PythonMacroService &service, QObject *parent = nullptr);

    ~PythonMacroJob() override;

    void start(const QString &path, const QString &text);

    void cancel();

signals:
    void completed(const QString &text, const QString &error);

private:
    QPointer<PythonMacroService> service;
    bool started = false;
    bool cancelled = false;
};
