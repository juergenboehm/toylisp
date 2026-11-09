#include "jsonprocess.h"
#include <QJsonDocument>
#include <QProcess>

namespace {
constexpr int maxMessageBytes = 256 * 1024;
constexpr int maxDiagnosticBytes = 4096;
constexpr int maxQueuedRequests = 16;
constexpr int startupTimeoutMs = 5000;
}

JsonProcess::JsonProcess(int requestTimeoutMs)
    : requestTimeoutMs(requestTimeoutMs)
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [this] {
        fail(ready ? "Process request timed out" : "Process did not report readiness in time");
    });
}

JsonProcess::~JsonProcess()
{
    for (auto *child : findChildren<QProcess *>(QString(), Qt::FindDirectChildrenOnly)) {
        child->disconnect(this);
        if (child->state() != QProcess::NotRunning) {
            child->kill();
            child->waitForFinished(1000);
        }
    }
}

bool JsonProcess::isConfigured() const
{
    return !program.isEmpty();
}

void JsonProcess::configure(const QString &newProgram, const QStringList &newArguments)
{
    if (program == newProgram && arguments == newArguments)
        return;
    program = newProgram;
    arguments = newArguments;
    fail("Process configuration changed");
    start();
}

void JsonProcess::start()
{
    if (process || !isConfigured())
        return;

    auto *child = new QProcess(this);
    process = child;
    connect(child, &QProcess::readyReadStandardOutput, this, &JsonProcess::readOutput);
    connect(child, &QProcess::readyReadStandardError, this, &JsonProcess::readDiagnostics);
    connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError) {
        readDiagnostics();
        fail(child->errorString());
    });
    connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                readDiagnostics();
                fail(QString("Process exited (code %1)").arg(code));
            });

    timer.start(startupTimeoutMs);
    child->start(program, arguments);
}

void JsonProcess::call(
    const QString &method, const QJsonObject &params, QObject *context, Completion completion)
{
    if (!isConfigured()) {
        completion({}, "Process executable is not configured");
        return;
    }

    if (waiting.size() >= maxQueuedRequests) {
        completion({}, "Process request queue is full");
        return;
    }

    const QString id = QString::number(++lastId);
    const QJsonObject message{{"id", id}, {"method", method}, {"params", params}};
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > maxMessageBytes) {
        completion({}, "Process request exceeds the size limit");
        return;
    }

    waiting.enqueue({id, std::move(line), context, std::move(completion)});
    start();
    sendNext();
}

void JsonProcess::cancel(QObject *context)
{
    for (int i = waiting.size() - 1; i >= 0; --i) {
        if (waiting[i].context == context)
            waiting.removeAt(i);
    }
    if (active && active->context == context)
        active->context.clear();
}

void JsonProcess::sendNext()
{
    if (!ready || active)
        return;
    while (!waiting.isEmpty() && !waiting.head().context)
        waiting.dequeue();
    if (waiting.isEmpty())
        return;

    active = waiting.dequeue();
    const QByteArray line = active->line;
    timer.start(requestTimeoutMs);
    if (process->write(line) != line.size())
        fail("Cannot write to process");
}

void JsonProcess::readOutput()
{
    stdoutBuffer += process->readAllStandardOutput();
    while (process) {
        const int end = stdoutBuffer.indexOf('\n');
        if (end >= maxMessageBytes || (end < 0 && stdoutBuffer.size() >= maxMessageBytes)) {
            fail("Process response exceeds the size limit");
            return;
        }
        if (end < 0)
            return;

        const QJsonObject message = QJsonDocument::fromJson(stdoutBuffer.left(end)).object();
        stdoutBuffer.remove(0, end + 1);
        if (!ready && message.value("ready") == QJsonValue(1)) {
            ready = true;
            timer.stop();
            sendNext();
            continue;
        }

        const auto result = message.value("result");
        const auto error = message.value("error");
        const bool success = result.isObject() && error.isUndefined();
        const bool failure = error.isString() && !error.toString().isEmpty() && result.isUndefined();
        if (!active || message.value("id") != QJsonValue(active->id) || !(success || failure)) {
            fail("Invalid process response");
            return;
        }

        timer.stop();
        auto request = std::move(*active);
        active.reset();
        if (request.context)
            request.completion(result.toObject(), error.toString());
        sendNext();
    }
}

void JsonProcess::readDiagnostics()
{
    stderrTail = (stderrTail + process->readAllStandardError()).right(maxDiagnosticBytes);
}

void JsonProcess::fail(const QString &error)
{
    timer.stop();
    ready = false;
    stdoutBuffer.clear();

    const QString diagnostics = QString::fromUtf8(stderrTail).trimmed();
    stderrTail.clear();

    if (auto *child = process) {
        process = nullptr;
        child->disconnect(this);
        if (child->state() == QProcess::NotRunning) {
            child->deleteLater();
        } else {
            connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), child,
                    &QObject::deleteLater);
            connect(child, &QProcess::errorOccurred, child, [child](QProcess::ProcessError code) {
                if (code == QProcess::FailedToStart)
                    child->deleteLater();
            });
            child->kill();
        }
    }

    auto requests = std::move(waiting);
    waiting.clear();
    if (active) {
        requests.prepend(std::move(*active));
        active.reset();
    }

    const QString message = diagnostics.isEmpty() ? error : error + "\n" + diagnostics;
    for (const auto &request : requests) {
        if (request.context)
            request.completion({}, message);
    }
}
