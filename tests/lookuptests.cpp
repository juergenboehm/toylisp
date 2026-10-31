#include "lookups/sanskritevaluator.h"
#include "lookups/transliteration.h"
#include "ui/editor.h"
#include "ui/mainwindow.h"
#include <functional>
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QSettings>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QUrlQuery>
#include <QtTest>

namespace {
QByteArray fixture(const QString &name)
{
    const QString path = QFINDTESTDATA("fixtures/sanskrit/" + name);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing lookup fixture");
    return file.readAll();
}

QByteArray dictionary(
    const QString &word, const QString &meaning, bool german = false, qint64 record = 0)
{
    const QString gloss = german ? "<hi rendition=\"#i\">" + meaning + "</hi>" : meaning;
    const QString tail
        = record > 0 ? QString("<note ana=\"tail\"><idno ana=\"L\">%1</idno></note>").arg(record)
                     : QString();
    const QString xml = "<entry xmlns=\"http://www.tei-c.org/ns/1.0\"><form><orth ana=\"key1\">"
                        + word + "</orth></form><sense>" + gloss + tail + "</sense></entry>";
    return QJsonDocument(
               QJsonObject{
                   {"data",
                    QJsonObject{
                        {"entries",
                         QJsonArray{QJsonObject{{"headword_slp1", word}, {"xml", xml}}}}}}})
        .toJson();
}
const QByteArray emptyDictionary = "{\"data\":{\"entries\":[]}}";

struct Reply
{
    QByteArray body;
    int status = 200;
    int delayMs = 0;
};
class LookupServer : public QTcpServer
{
public:
    QList<QUrl> requests;
    std::function<Reply(const QUrl &)> handler;
    LookupServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QIODevice::readyRead, this, [this, socket, buffer] {
                    *buffer += socket->readAll();
                    if (!buffer->contains("\r\n\r\n"))
                        return;
                    socket->disconnect(this);
                    const QUrl url = QUrl::fromEncoded(buffer->split(' ').value(1));
                    requests.append(url);
                    const Reply reply = handler ? handler(url) : Reply{emptyDictionary};
                    if (reply.delayMs < 0)
                        return;
                    QTimer::singleShot(reply.delayMs, socket, [socket, reply] {
                        if (socket->state() != QAbstractSocket::ConnectedState)
                            return;
                        socket->write(
                            "HTTP/1.1 " + QByteArray::number(reply.status)
                            + " Test\r\nContent-Length: " + QByteArray::number(reply.body.size())
                            + "\r\nConnection: close\r\n\r\n" + reply.body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost);
    }
    SanskritEndpoints endpoints() const
    {
        const QUrl base(QString("http://127.0.0.1:%1/").arg(serverPort()));
        return {base.resolved(QUrl("dicts/")), base.resolved(QUrl("getWord.php"))};
    }
};

class DelayedEvaluator : public Evaluator
{
public:
    QList<QPointer<EvaluationJob>> jobs;
    EvaluationJob *startEvaluation(const QString &, QObject *parent) override
    {
        auto *job = new EvaluationJob(parent);
        jobs.append(job);
        return job;
    }
};

void evaluate(Editor &editor, int marker = 0)
{
    QTextBlock block = editor.document()->firstBlock();
    while (block.isValid()) {
        if (block.text().startsWith("==") && marker-- == 0) {
            QTextCursor cursor(block);
            cursor.movePosition(QTextCursor::EndOfBlock);
            editor.setTextCursor(cursor);
            QTest::keyClick(&editor, Qt::Key_Return);
            return;
        }
        block = block.next();
    }
    qFatal("Missing test evaluation marker");
}
} // namespace

class LookupTests : public QObject
{
    Q_OBJECT
    QTemporaryDir settingsDirectory;
private slots:
    void initTestCase()
    {
        QVERIFY(settingsDirectory.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    }
    void transliteration_data()
    {
        QTest::addColumn<QString>("deva");
        QTest::addColumn<QString>("slp");
        QTest::newRow("noun") << QString::fromUtf8("रामेण") << "rAmeRa";
        QTest::newRow("verb") << QString::fromUtf8("गच्छति") << "gacCati";
        QTest::newRow("root") << QString::fromUtf8("गम्") << "gam";
        QTest::newRow("bhu") << QString::fromUtf8("भू") << "BU";
        QTest::newRow("vocalic-r") << QString::fromUtf8("कृष्ण") << "kfzRa";
        QTest::newRow("vowels") << QString::fromUtf8("अआइईउऊऋॠऌॡएऐओऔ") << "aAiIuUfFxXeEoO";
        QTest::newRow("visarga") << QString::fromUtf8("अग्निः") << "agniH";
        QTest::newRow("anusvara") << QString::fromUtf8("संस्कृतम्") << "saMskftam";
        QTest::newRow("avagraha") << QString::fromUtf8("सोऽहम्") << "so'ham";
        QTest::newRow("chandrabindu") << QString::fromUtf8("अँ") << "a~";
        QTest::newRow("duratas") << QString::fromUtf8("दूरतस्") << "dUratas";
        QTest::newRow("duratasa") << QString::fromUtf8("दूरतस") << "dUratasa";
        QTest::newRow("vasudhaiva") << QString::fromUtf8("वसुधैव") << "vasuDEva";
        QTest::newRow("short-bhu") << QString::fromUtf8("भु") << "Bu";
        QTest::newRow("kutumbakam") << QString::fromUtf8("कुटुम्बकम्") << "kuwumbakam";
    }
    void transliteration()
    {
        QFETCH(QString, deva);
        QFETCH(QString, slp);
        QString result, error;
        QVERIFY2(devanagariToSlp1(deva, result, error), qPrintable(error));
        QCOMPARE(result, slp);
        QCOMPARE(slp1ToDevanagari(result), deva);
        QVERIFY(isSlp1Word(slp));
    }
    void invalidInput()
    {
        for (const auto &input :
             {QString(),
              QString("agni"),
              QString::fromUtf8("राम कृष्ण"),
              QString::fromUtf8("राम\nकृष्ण"),
              QString::fromUtf8("ि"),
              QString::fromUtf8("काि"),
              QString::fromUtf8("क़"),
              QString::fromUtf8("ॐ"),
              QString(129, QChar(0x0905))}) {
            QString result, error;
            QVERIFY(!devanagariToSlp1(input, result, error));
            QVERIFY(!error.isEmpty());
        }
        QString result, error;
        QVERIFY(devanagariToSlp1(QString::fromUtf8("  ग्‍च्छति\n"), result, error));
        QCOMPARE(result, QString("gcCati"));
    }
    void morphologyFixtures()
    {
        auto result = parseInflectedForms(fixture("ramena.html"));
        QVERIFY(result.ok);
        QCOMPARE(result.values, QStringList{"rAma"});
        result = parseInflectedForms(fixture("gacchati.html"));
        QCOMPARE(result.values, QStringList{"gam"});
        result = parseInflectedForms(fixture("bhavati.html"));
        QCOMPARE(result.values, (QStringList{"BavatI", "Bavat", "BU"}));
        result = parseInflectedForms(fixture("kutumbakam.html"));
        QVERIFY(result.ok);
        QCOMPARE(result.values, QStringList{"kuwumbaka"});
        QVERIFY(parseInflectedForms("\n<disp1>\n").ok);
        QVERIFY(!parseInflectedForms("<html>Server error</html>").ok);
        QVERIFY(!parseInflectedForms("<disp1>id_info('https://x/?key=invalid123')").ok);
        QByteArray manyForms = "<disp1>";
        for (int i = 1; i <= 17; ++i)
            manyForms += "id_info('https://x/?key=" + QByteArray(i, 'a') + "')";
        QVERIFY(!parseInflectedForms(manyForms).ok);
    }
    void dictionaryFixtures()
    {
        auto result
            = parseSanskritDictionary(fixture("mw-agni.json"), "agni", SanskritLanguage::English);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.values.size(), 9);
        QVERIFY(result.values.first().startsWith("fire, sacrificial fire"));
        QVERIFY(result.values.contains("gold"));
        QVERIFY(!result.values.join('\n').contains("fire-particle"));
        QVERIFY(!result.values.join('\n').contains("monier_"));
        result = parseSanskritDictionary(fixture("pwg-gam.json"), "gam", SanskritLanguage::German);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.values.size(), 10); // Seven verb senses and three homonymous entries.
        QVERIFY(result.values.first().startsWith("gehen, sich bewegen"));
        QVERIFY(!result.values.join('\n').contains("Gang, Art zu gehen"));
        QVERIFY(!result.values.join('\n').contains("wenn dein Herz Furcht"));
        result = parseSanskritDictionary(fixture("mw-bhu.json"), "BU", SanskritLanguage::English);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.values.first().startsWith("to become, be"));
        QVERIFY(!result.values.first().contains("baB"));
        QVERIFY(!result.values.first().contains("Dha1tup"));
    }
    void invalidDictionaryResponses()
    {
        for (const auto &json :
             {QByteArray("invalid"),
              QByteArray("[]"),
              QByteArray("{}"),
              QByteArray("{\"data\":{\"entries\":{}}}"),
              dictionary("other", "meaning"),
              dictionary("agni", "<unclosed>")})
            QVERIFY(!parseSanskritDictionary(json, "agni", SanskritLanguage::English).ok);
        QVERIFY(parseSanskritDictionary(emptyDictionary, "agni", SanskritLanguage::German).ok);
        QVERIFY(parseSanskritDictionary(emptyDictionary, "agni", SanskritLanguage::German)
                    .values.isEmpty());
        QVERIFY(
            !parseSanskritDictionary(dictionary("gam", "no gloss"), "gam", SanskritLanguage::German)
                 .ok);
    }
    void germanSubSensesAndPartialEntries()
    {
        auto result = parseSanskritDictionary(
            fixture("pwg-vasudha.json"), "vasuDA", SanskritLanguage::German);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.values.size(), 3);
        QVERIFY(result.values.contains("die Erde; Land, Reich"));
        QVERIFY(result.values.join('\n').contains("Anapaest"));
        QVERIFY(!result.values.join('\n').contains("regiert habend"));

        result = parseSanskritDictionary(fixture("pwg-eva.json"), "eva", SanskritLanguage::German);
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.values.join('\n').contains("gerade, eben, kaum, nur, noch, schon"));

        // An unsupported homonym must not hide valid entries, in either order.
        const auto valid = QJsonDocument::fromJson(dictionary("eva", "nur", true))
                               .object()["data"]
                               .toObject()["entries"]
                               .toArray()
                               .first();
        const auto unsupported = QJsonDocument::fromJson(dictionary("eva", "vgl. anderes Wort"))
                                     .object()["data"]
                                     .toObject()["entries"]
                                     .toArray()
                                     .first();
        for (const auto &entries :
             {QJsonArray{valid, unsupported}, QJsonArray{unsupported, valid}}) {
            result = parseSanskritDictionary(
                QJsonDocument(QJsonObject{{"data", QJsonObject{{"entries", entries}}}}).toJson(),
                "eva",
                SanskritLanguage::German);
            QVERIFY(!result.ok);
            QCOMPARE(result.values, QStringList{"nur"});
        }
    }
    void subentryAndPreviousDefinitionFixtures()
    {
        auto result = parseSanskritSubentries(
            fixture("mw-kutumba.json"), "kuwumbaka", SanskritLanguage::English);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.values.size(), 3);
        QCOMPARE(result.values.first(), QString("a household, family"));
        QVERIFY(!result.values.join('\n').contains("domestic dissension"));
        QVERIFY(!result.values.join('\n').contains("an attentive father"));
        QVERIFY(!parseSanskritDictionary(
                     fixture("mw-kutumba.json"), "kuwumbaka", SanskritLanguage::English)
                     .ok);
        QVERIFY(
            !parseSanskritSubentries(fixture("mw-kutumba.json"), "other", SanskritLanguage::English)
                 .ok);

        result = parseSanskritDictionary(
            fixture("pwg-kutumbaka.json"), "kuwumbaka", SanskritLanguage::German);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.previousEntries, QList<qint64>{17548});
        QVERIFY(result.values.first().startsWith("eine best. Grasart"));
        QVERIFY(!result.values.join('\n').contains("ayaM"));

        result = parseSanskritPreviousEntry(fixture("pwg-before-kutumbaka.json"), 17548);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(result.values.first().startsWith(QString::fromUtf8("wie कुटुम्ब: Hausstand")));
        QVERIFY(result.previousEntries.isEmpty());
        result = parseSanskritPreviousEntry(fixture("pwg-before-kutumbaka.json"), 17547);
        QVERIFY(!result.ok);
        QVERIFY(result.values.isEmpty());
        // A citation containing the searched number is not the referenced record.
        result = parseSanskritPreviousEntry(dictionary("rAma", "citation 17548", true, 900), 17548);
        QVERIFY(!result.ok);
        QVERIFY(result.values.isEmpty());
        QVERIFY(!parseSanskritPreviousEntry(emptyDictionary, 17548).ok);
        QVERIFY(!parseSanskritPreviousEntry(emptyDictionary, 0).ok);
        auto ambiguous = QJsonDocument::fromJson(dictionary("rAma", "first", true, 17548)).object();
        auto entries = ambiguous["data"].toObject()["entries"].toArray();
        entries.append(
            QJsonDocument::fromJson(dictionary("agni", "second", true, 17548))
                .object()["data"]
                .toObject()["entries"]
                .toArray()
                .first());
        ambiguous["data"] = QJsonObject{{"entries", entries}};
        result = parseSanskritPreviousEntry(QJsonDocument(ambiguous).toJson(), 17548);
        QVERIFY(!result.ok);
        QVERIFY(result.values.isEmpty());
        QVERIFY(result.previousEntries.isEmpty());

        result
            = parseSanskritDictionary(dictionary("rAma", "dass."), "rAma", SanskritLanguage::German);
        QVERIFY(!result.ok); // Never guess a predecessor from the spelling alone.
        QVERIFY(result.previousEntries.isEmpty());
        result = parseSanskritDictionary(
            dictionary("rAma", "<sense n=\"1)\">1) dass.</sense>", false, 900),
            "rAma",
            SanskritLanguage::German);
        QVERIFY(result.previousEntries.isEmpty()); // A numbered sense may refer within the entry.
    }
    void responseLimits()
    {
        auto response = QJsonDocument::fromJson(dictionary("agni", "fire")).object();
        const auto entry = response["data"].toObject()["entries"].toArray().first();
        QJsonArray full;
        for (int i = 0; i < 20; ++i)
            full.append(entry);
        response["data"] = QJsonObject{{"entries", full}};
        QVERIFY(!parseSanskritDictionary(
                     QJsonDocument(response).toJson(), "agni", SanskritLanguage::English)
                     .ok);
        const QString deeplyNested = QString("<sense>").repeated(70) + "fire"
                                     + QString("</sense>").repeated(70);
        QVERIFY(!parseSanskritDictionary(
                     dictionary("agni", deeplyNested), "agni", SanskritLanguage::English)
                     .ok);
    }
    void networkLookupAndLanguageSnapshot()
    {
        LookupServer server;
        QVERIFY2(server.isListening(), qPrintable(server.errorString()));
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {fixture("gacchati.html"), 200, 30};
            const QString word = QUrlQuery(url).queryItemValue("query");
            return {word == "gam" ? fixture("pwg-gam.json") : emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("गच्छति"), this));
        evaluator.setLanguage(SanskritLanguage::English);
        QVERIFY(!job->isFinished());
        QTRY_VERIFY(job->isFinished());
        QVERIFY2(job->result().ok, qPrintable(job->result().text));
        QVERIFY(job->result().text.startsWith(QString::fromUtf8("गम् — gehen")));
        for (const auto &line : job->result().text.split('\n'))
            QVERIFY(line.startsWith(QString::fromUtf8("गम् — ")));
        QCOMPARE(server.requests.size(), 3);
        for (const auto &url : server.requests) {
            if (url.path().contains("entries"))
                QVERIFY(url.path().contains("/pwg/"));
            else
                QCOMPARE(QUrlQuery(url).queryItemValue("word"), QString("gacCati"));
        }
    }
    void directAndMorphologicalMeanings()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {fixture("bhavati.html")};
            const QString word = QUrlQuery(url).queryItemValue("query");
            if (word == "Bavati")
                return {dictionary(word, "direct meaning")};
            if (word == "BU")
                return {dictionary(word, "to become")};
            return {emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        evaluator.setLanguage(SanskritLanguage::English);
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("भवति"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(job->result().ok);
        QCOMPARE(job->result().text, QString::fromUtf8("भवति — direct meaning\nभू — to become"));
        QCOMPARE(server.requests.size(), 7); // Two missing MW headwords also try the subentry index.
    }
    void noMatchAndPartialFailure()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            return {url.path() == "/getWord.php" ? QByteArray("<disp1>\n") : emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(evaluator.startEvaluation(QString::fromUtf8("राम"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(job->result().ok);
        QVERIFY(job->result().text.startsWith("No dictionary entry"));
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {"unavailable", 503};
            return {dictionary("rAma", "angenehm", true)};
        };
        job.reset(evaluator.startEvaluation(QString::fromUtf8("राम"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(!job->result().ok);
        QVERIFY(job->result().text.startsWith(QString::fromUtf8("राम — angenehm")));
        QVERIFY(job->result().text.contains("HTTP 503"));
    }
    void reportedGermanWords_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<bool>("complete");
        QTest::newRow("duratas") << QString::fromUtf8("दूरतस्") << "aus der Ferne her" << true;
        QTest::newRow("duratasa") << QString::fromUtf8("दूरतस") << "No dictionary entry" << true;
        QTest::newRow("vasudha") << QString::fromUtf8("वसुधा") << "die Erde; Land, Reich" << true;
        QTest::newRow("eva") << QString::fromUtf8("एव") << "gerade, eben, kaum, nur" << false;
        QTest::newRow("sandhi") << QString::fromUtf8("वसुधैव") << "No dictionary entry" << true;
    }
    void reportedGermanWords()
    {
        QFETCH(QString, word);
        QFETCH(QString, expected);
        QFETCH(bool, complete);
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {"<disp1>\n"};
            const QString query = QUrlQuery(url).queryItemValue("query");
            if (query == "dUratas")
                return {fixture("pwg-duratas.json")};
            if (query == "vasuDA")
                return {fixture("pwg-vasudha.json")};
            if (query == "eva")
                return {fixture("pwg-eva.json")};
            return {emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(evaluator.startEvaluation(word, this));
        QTRY_VERIFY(job->isFinished());
        QCOMPARE(job->result().ok, complete);
        QVERIFY2(job->result().text.contains(expected), qPrintable(job->result().text));
        QCOMPARE(job->result().text.contains("Lookup incomplete:"), !complete);
        QCOMPARE(server.requests.size(), 2);
    }
    void kutumbakamLookup_data()
    {
        QTest::addColumn<bool>("german");
        QTest::newRow("pwg-reference") << true;
        QTest::newRow("mw-subentry") << false;
    }
    void kutumbakamLookup()
    {
        QFETCH(bool, german);
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [german](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {fixture("kutumbakam.html")};
            const QUrlQuery query(url);
            const QString field = query.queryItemValue("field");
            const QString word = query.queryItemValue("query");
            if (german && field == "headword_slp1" && word == "kuwumbaka")
                return {fixture("pwg-kutumbaka.json")};
            if (german && field == "xml" && word == "17548")
                return {fixture("pwg-before-kutumbaka.json")};
            if (!german && field == "re_headwords_slp1" && word == "kuwumbaka")
                return {fixture("mw-kutumba.json")};
            return {emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        evaluator.setLanguage(german ? SanskritLanguage::German : SanskritLanguage::English);
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("कुटुम्बकम्"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY2(job->result().ok, qPrintable(job->result().text));
        QVERIFY(job->result().text.contains(
            german ? "Hausstand, Hauswesen; Hausgesinde, Familie" : "a household, family"));
        for (const auto &line : job->result().text.split('\n'))
            QVERIFY(line.startsWith(QString::fromUtf8("कुटुम्बक — ")));
        QVERIFY(!job->result().text.contains("domestic dissension"));
        QCOMPARE(server.requests.size(), german ? 4 : 5);
    }
    void recognizedBaseWithoutMeanings_data()
    {
        QTest::addColumn<bool>("german");
        QTest::newRow("pwg") << true;
        QTest::newRow("mw") << false;
    }
    void recognizedBaseWithoutMeanings()
    {
        QFETCH(bool, german);
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            return {url.path() == "/getWord.php" ? fixture("kutumbakam.html") : emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        evaluator.setLanguage(german ? SanskritLanguage::German : SanskritLanguage::English);
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("कुटुम्बकम्"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(job->result().ok);
        QVERIFY(job->result().text.contains(QString::fromUtf8("Recognized base form(s): कुटुम्बक")));
        QVERIFY(job->result().text.contains(
            german ? "No dictionary meanings found in PWG" : "No dictionary meanings found in MW"));
        QVERIFY(!job->result().text.contains("No dictionary entry or supported base form"));
    }
    void referenceFailureKeepsOtherMeanings()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {fixture("kutumbakam.html")};
            const QUrlQuery query(url);
            if (query.queryItemValue("field") == "xml")
                return {"unavailable", 503};
            return {
                query.queryItemValue("query") == "kuwumbaka" ? fixture("pwg-kutumbaka.json")
                                                             : emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("कुटुम्बकम्"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(!job->result().ok);
        QVERIFY(job->result().text.contains("eine best. Grasart"));
        QVERIFY(job->result().text.contains("HTTP 503"));
        QVERIFY(!job->result().text.contains("Hausstand"));
    }
    void referenceChainIsBounded()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {"<disp1>\n"};
            const QUrlQuery query(url);
            const qint64 record = query.queryItemValue("field") == "xml"
                                      ? query.queryItemValue("query").toLongLong()
                                      : 100;
            return {dictionary("rAma", "dass.", false, record)};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(evaluator.startEvaluation(QString::fromUtf8("राम"), this));
        QTRY_VERIFY(job->isFinished());
        QVERIFY(!job->result().ok);
        QVERIFY(job->result().text.contains("Dictionary reference limit reached"));
        QCOMPARE(server.requests.size(), 6); // Direct lookup, morphology and four predecessor hops.
    }
    void cancellingPendingReference()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            if (url.path() == "/getWord.php")
                return {fixture("kutumbakam.html")};
            const QUrlQuery query(url);
            if (query.queryItemValue("field") == "xml")
                return {dictionary("kuwumba", "dass.", false, 17548), 200, 100};
            return {
                query.queryItemValue("query") == "kuwumbaka" ? fixture("pwg-kutumbaka.json")
                                                             : emptyDictionary};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("कुटुम्बकम्"), this));
        QTRY_COMPARE(server.requests.size(), 4);
        QSignalSpy completed(job.data(), &EvaluationJob::completed);
        job->cancel();
        QTest::qWait(200);
        QCOMPARE(completed.count(), 0);
        QCOMPARE(server.requests.size(), 4);
    }
    void inputValidationDoesNotUseNetwork()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(evaluator.startEvaluation("agni", this));
        QVERIFY(job->isFinished());
        QVERIFY(!job->result().ok);
        QTest::qWait(20);
        QVERIFY(server.requests.isEmpty());
    }
    void cancellingLookupStopsDerivedRequests()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        server.handler = [](const QUrl &url) -> Reply {
            return {
                url.path() == "/getWord.php" ? fixture("gacchati.html") : emptyDictionary, 200, 80};
        };
        SanskritEvaluator evaluator(server.endpoints());
        QScopedPointer<EvaluationJob> job(
            evaluator.startEvaluation(QString::fromUtf8("गच्छति"), this));
        QTRY_COMPARE(server.requests.size(), 2);
        QSignalSpy completed(job.data(), &EvaluationJob::completed);
        job->cancel();
        QTest::qWait(150);
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(completed.count(), 0);
        QVERIFY(job->isCancelled());
    }
    void httpTimeoutLimitAndCancellation()
    {
        LookupServer server;
        QVERIFY(server.isListening());
        const QUrl url = server.endpoints().inflectedForms;
        server.handler = [](const QUrl &) -> Reply { return {{}, 200, -1}; };
        HttpClient client({60, 32});
        QScopedPointer<HttpRequest> request(client.get(url, this));
        QSignalSpy finished(request.data(), &HttpRequest::finished);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!request->result().ok);
        QCOMPARE(request->result().error, QString("Request timed out"));
        server.handler = [](const QUrl &) -> Reply { return {QByteArray(64, 'x')}; };
        request.reset(client.get(url, this));
        QSignalSpy tooLarge(request.data(), &HttpRequest::finished);
        QTRY_COMPARE(tooLarge.count(), 1);
        QVERIFY(request->result().error.contains("size limit"));
        server.handler = [](const QUrl &) -> Reply { return {{}, 200, -1}; };
        request.reset(client.get(url, this));
        QSignalSpy cancelled(request.data(), &HttpRequest::finished);
        request->cancel();
        QTest::qWait(100);
        QCOMPARE(cancelled.count(), 0);
    }
    void delayedReplacementUndoAndOtherEdits()
    {
        EvaluatorRegistry registry;
        auto implementation = std::make_unique<DelayedEvaluator>();
        auto *delayed = implementation.get();
        registry.addEvaluator("delayed", std::move(implementation));
        Editor editor(registry);
        editor.setPlainText("prefix\n>>\ninput\n==delayed\nold\n##\ntail");
        const QString before = editor.toPlainText();
        evaluate(editor);
        QCOMPARE(editor.toPlainText(), before);
        QVERIFY(!editor.document()->isModified());
        QCOMPARE(editor.activeEvaluationCount(), 1);
        QTextCursor cursor(editor.document());
        cursor.insertText("unrelated\n");
        editor.setTextCursor(cursor);
        const auto cursorPosition = editor.textCursor().position();
        delayed->jobs.last()->finish({true, "first\nsecond"});
        QVERIFY(editor.toPlainText().contains("==delayed\nfirst\nsecond\n##\ntail"));
        QCOMPARE(editor.textCursor().position(), cursorPosition);
        QCOMPARE(editor.activeEvaluationCount(), 0);
        const auto result = editor.toPlainText();
        editor.undo();
        QCOMPARE(editor.toPlainText(), "unrelated\n" + before);
        editor.redo();
        QCOMPARE(editor.toPlainText(), result);
    }
    void changedBlocksAndSupersededRequests()
    {
        EvaluatorRegistry registry;
        auto implementation = std::make_unique<DelayedEvaluator>();
        auto *delayed = implementation.get();
        registry.addEvaluator("delayed", std::move(implementation));
        Editor editor(registry);
        editor.setPlainText(">>\ninput\n==delayed\nold\n##");
        evaluate(editor);
        auto old = delayed->jobs.last();
        evaluate(editor);
        QVERIFY(old->isCancelled());
        old->finish({true, "stale"});
        QVERIFY(!editor.toPlainText().contains("stale"));
        auto current = delayed->jobs.last();
        QTextCursor cursor(editor.document()->findBlockByNumber(1));
        cursor.insertText("changed");
        QVERIFY(current->isCancelled());
        current->finish({true, "late"});
        QVERIFY(!editor.toPlainText().contains("late"));
        editor.undo();
        evaluate(editor);
        current = delayed->jobs.last();
        cursor = QTextCursor(editor.document()->findBlockByNumber(3));
        cursor.insertText("manual output edit");
        QVERIFY(current->isCancelled());
        editor.cancelEvaluations();
        QCOMPARE(editor.activeEvaluationCount(), 0);
    }
    void independentBlocksAndEditorDestruction()
    {
        EvaluatorRegistry registry;
        auto implementation = std::make_unique<DelayedEvaluator>();
        auto *delayed = implementation.get();
        registry.addEvaluator("delayed", std::move(implementation));
        {
            Editor editor(registry);
            editor.setPlainText(">>\none\n==delayed\n>>\ntwo\n==delayed");
            evaluate(editor);
            auto first = delayed->jobs.last();
            evaluate(editor, 1);
            auto second = delayed->jobs.last();
            first->finish({true, "one result"});
            QVERIFY(!second->isCancelled());
            second->finish({true, "two result"});
            QVERIFY(editor.toPlainText().contains(
                "one result\n##\n>>\ntwo\n==delayed\ntwo result\n##"));
            evaluate(editor);
        }
        QVERIFY(delayed->jobs.last().isNull());
    }
    void languageMenuAndPersistence()
    {
        QSettings("ToyLisp", "ToyLisp").clear();
        {
            MainWindow window;
            auto *german = window.findChild<QAction *>("sanskritLanguage_de");
            auto *english = window.findChild<QAction *>("sanskritLanguage_en");
            QVERIFY(german && english);
            QVERIFY(german->isChecked());
            english->trigger();
            QVERIFY(english->isChecked());
            QVERIFY(!german->isChecked());
            auto *selector = window.findChild<QComboBox *>("evaluatorSelector");
            QCOMPARE(
                selector->itemData(selector->findData("sanskrit")).toString(), QString("sanskrit"));
            auto *editor = window.findChild<Editor *>();
            selector->setCurrentIndex(selector->findData("sanskrit"));
            editor->setPlainText(">>\nnot devanagari\n==");
            evaluate(*editor);
            QVERIFY(editor->toPlainText().contains("Enter one Devanagari word"));
            editor->document()->setModified(false);
        }
        MainWindow reopened;
        QVERIFY(reopened.findChild<QAction *>("sanskritLanguage_en")->isChecked());
        QCOMPARE(
            QSettings("ToyLisp", "ToyLisp").value("sanskrit/resultLanguage").toString(),
            QString("en"));
    }
    void sanskritWindowScreenshot()
    {
        MainWindow window;
        window.findChild<QAction *>("sanskritLanguage_de")->trigger();
        auto *selector = window.findChild<QComboBox *>("evaluatorSelector");
        selector->setCurrentIndex(selector->findData("sanskrit"));
        const auto parsed
            = parseSanskritDictionary(fixture("pwg-gam.json"), "gam", SanskritLanguage::German);
        QVERIFY(parsed.ok);
        QStringList lines;
        for (const auto &meaning : parsed.values)
            lines.append(QString::fromUtf8("गम् — ") + meaning);
        auto *editor = window.findChild<Editor *>();
        editor->setPlainText(
            QString::fromUtf8(">>\nगच्छति\n==sanskrit\n") + lines.join('\n') + "\n##");
        editor->document()->setModified(false);
        window.show();
        QTest::qWait(50);
        QVERIFY(window.grab().save("toylisp-sanskrit-screenshot.png"));
    }
    void liveCologne()
    {
        if (!qEnvironmentVariableIsSet("TOYLISP_LIVE_LOOKUP"))
            QSKIP("Opt-in live network check; offline tests cover normal CTest runs");
        qInfo() << "TLS build:" << QSslSocket::sslLibraryBuildVersionString()
                << "runtime:" << QSslSocket::sslLibraryVersionString();
        QVERIFY2(QSslSocket::supportsSsl(), "Qt cannot load a compatible TLS runtime");
        SanskritEvaluator evaluator({}, {30000, 4 * 1024 * 1024});
        for (const auto language : {SanskritLanguage::German, SanskritLanguage::English}) {
            evaluator.setLanguage(language);
            QScopedPointer<EvaluationJob> job(
                evaluator.startEvaluation(QString::fromUtf8("गच्छति"), this));
            QTRY_VERIFY_WITH_TIMEOUT(job->isFinished(), 65000);
            QVERIFY2(job->result().ok, qPrintable(job->result().text));
            QVERIFY(job->result().text.contains(QString::fromUtf8("गम् — ")));
            qInfo().noquote() << job->result().text.left(800);
            job.reset(evaluator.startEvaluation(QString::fromUtf8("कुटुम्बकम्"), this));
            QTRY_VERIFY_WITH_TIMEOUT(job->isFinished(), 65000);
            QVERIFY2(job->result().ok, qPrintable(job->result().text));
            QVERIFY(job->result().text.contains(
                language == SanskritLanguage::German ? "Hausstand, Hauswesen; Hausgesinde, Familie"
                                                     : "a household, family"));
            for (const auto &line : job->result().text.split('\n'))
                QVERIFY(line.startsWith(QString::fromUtf8("कुटुम्बक — ")));
            qInfo().noquote() << job->result().text.left(800);
        }
        evaluator.setLanguage(SanskritLanguage::German);
        const QList<QPair<QString, QString>> examples{
            {QString::fromUtf8("दूरतस्"), "aus der Ferne her"},
            {QString::fromUtf8("वसुधा"), "die Erde; Land, Reich"},
            {QString::fromUtf8("एव"), "gerade, eben, kaum, nur"}};
        for (const auto &example : examples) {
            QScopedPointer<EvaluationJob> job(evaluator.startEvaluation(example.first, this));
            QTRY_VERIFY_WITH_TIMEOUT(job->isFinished(), 65000);
            QVERIFY2(job->result().text.contains(example.second), qPrintable(job->result().text));
            QCOMPARE(job->result().ok, example.first != QString::fromUtf8("एव"));
            qInfo().noquote() << job->result().text.left(800);
        }
    }
};
QTEST_MAIN(LookupTests)
#include "lookuptests.moc"
