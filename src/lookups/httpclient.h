#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

struct HttpOptions
{
    int timeoutMs = 20000;
    qint64 maxResponseBytes = 4 * 1024 * 1024;
};

struct HttpResponse
{
    bool ok = false;
    QByteArray body;
    QString error;
    bool limitExceeded = false;
};

class HttpRequest : public QObject
{
    Q_OBJECT

public:
    HttpRequest(QNetworkReply *reply, const HttpOptions &options, QObject *parent);

    ~HttpRequest() override;

    const HttpResponse &result() const;

    void cancel();

signals:
    void finished();

private:
    QPointer<QNetworkReply> reply;
    QTimer timer;
    HttpResponse response;
    bool done = false;

    void readAvailable(qint64 limit);

    void complete(const QString &error = {});
};

class HttpClient
{
public:
    explicit HttpClient(const HttpOptions &options = {});

    HttpRequest *get(const QUrl &url, QObject *parent);

    HttpRequest *get(QNetworkRequest request, QObject *parent);

private:
    QNetworkAccessManager network;
    HttpOptions options;
};
