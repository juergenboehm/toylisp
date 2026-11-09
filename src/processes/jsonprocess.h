#pragma once

#include <functional>
#include <optional>
#include <QJsonObject>
#include <QPointer>
#include <QQueue>
#include <QStringList>
#include <QTimer>

class QProcess;

// An asynchronous, serial request channel over a child process's standard streams.
class JsonProcess : public QObject
{
public:
    using Completion = std::function<void(const QJsonObject &result, const QString &error)>;

    explicit JsonProcess(int requestTimeoutMs = 40000);

    ~JsonProcess() override;

    bool isConfigured() const;

    void configure(const QString &program, const QStringList &arguments);

    void call(
        const QString &method, const QJsonObject &params, QObject *context, Completion completion);

    void cancel(QObject *context);

private:
    struct Request
    {
        QString id;
        QByteArray line;
        QPointer<QObject> context;
        Completion completion;
    };

    QString program;
    QStringList arguments;
    QProcess *process = nullptr;
    QTimer timer;
    int requestTimeoutMs;
    bool ready = false;
    quint64 lastId = 0;
    QByteArray stdoutBuffer;
    QByteArray stderrTail;
    QQueue<Request> waiting;
    std::optional<Request> active;

    void start();

    void sendNext();

    void readOutput();

    void readDiagnostics();

    void fail(const QString &error);
};
