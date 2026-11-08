#include "httpclient.h"
#include <QNetworkReply>
#include <QNetworkRequest>

HttpRequest::HttpRequest(QNetworkReply *reply, const HttpOptions &options, QObject *parent)
    : QObject(parent)
    , reply(reply)
{
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [this] { complete("Request timed out"); });
    connect(reply, &QIODevice::readyRead, this, [this, options] {
        readAvailable(options.maxResponseBytes);
    });
    connect(reply, &QNetworkReply::finished, this, [this, options] {
        if (done)
            return;
        readAvailable(options.maxResponseBytes);
        if (done)
            return;
        const int status = this->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status && (status < 200 || status >= 300))
            complete(QString("HTTP %1").arg(status));
        else if (this->reply->error() != QNetworkReply::NoError)
            complete(this->reply->errorString());
        else
            complete();
    });
    timer.start(options.timeoutMs);
}

HttpRequest::~HttpRequest()
{
    cancel();
    if (reply)
        reply->deleteLater();
}

void HttpRequest::readAvailable(qint64 limit)
{
    if (done || !reply)
        return;
    if (response.body.size() + reply->bytesAvailable() > limit) {
        response.limitExceeded = true;
        complete("Response exceeds the size limit");
        return;
    }
    response.body += reply->readAll();
}

void HttpRequest::cancel()
{
    done = true;
    timer.stop();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
    }
}

void HttpRequest::complete(const QString &error)
{
    if (done)
        return;
    response.ok = error.isEmpty();
    response.error = error;
    cancel();
    emit finished();
}

HttpClient::HttpClient(const HttpOptions &options)
    : options(options)
{}

HttpRequest *HttpClient::get(const QUrl &url, QObject *parent)
{
    return get(QNetworkRequest(url), parent);
}

HttpRequest *HttpClient::get(QNetworkRequest request, QObject *parent)
{
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = network.get(request);
    reply->setReadBufferSize(options.maxResponseBytes + 1);
    return new HttpRequest(reply, options, parent);
}
