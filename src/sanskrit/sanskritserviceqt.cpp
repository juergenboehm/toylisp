#include "sanskritserviceqt.h"
#include "sanskritserviceqt_p.h"
#include "sanskritparser.h"
#include <QFile>
#include <QJsonArray>
#include <QUrlQuery>

static QString bundledDispatcher()
{
    Q_INIT_RESOURCE(sanskrit_python);
    QFile file(":/sanskrit/dispatcher.py");
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const auto script = QString::fromUtf8(file.readAll());
    return file.error() == QFileDevice::NoError ? script : QString{};
}

namespace sanskrit {
namespace {
HttpRequest *get(HttpClient &http, const QUrl &url, QObject *parent)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "SanskritServiceQt/0.1");
    request.setRawHeader("Accept", "application/json, text/html;q=0.9");
    return http.get(request, parent);
}

Issue requestIssue(const HttpResponse &response)
{
    return response.limitExceeded ? Issue::Limit : Issue::RequestFailed;
}

Issue parseIssue(const detail::LookupParseResult &parsed)
{
    return parsed.limitExceeded ? Issue::Limit : Issue::InvalidResponse;
}

std::vector<Meaning> meanings(
    const QStringList &values, const QString &source, std::optional<std::int64_t> record = {})
{
    std::vector<Meaning> result;
    const auto headword = source.toStdString();
    for (const auto &value : values)
        result.push_back({value.toStdString(), headword, record});
    return result;
}

Analysis analysisError(Issue issue, const QString &message)
{
    Analysis result;
    result.diagnostics.push_back({Stage::Analysis, issue, message.toStdString(), {}});
    return result;
}

std::vector<GrammarFeature> parseGrammar(const QString &tag)
{
    std::vector<GrammarFeature> features;
    for (const auto &item : tag.split(',')) {
        const auto feature = item.trimmed();
        if (feature.isEmpty())
            continue;
        const int separator = feature.indexOf('=');
        if (separator < 0)
            features.push_back({feature.toStdString(), {}});
        else
            features.push_back(
                {feature.left(separator).trimmed().toStdString(),
                 feature.mid(separator + 1).trimmed().toStdString()});
    }
    return features;
}

Analysis parseAnalysis(const QJsonObject &json)
{
    const auto words = json.value("words").toArray();
    if (words.isEmpty() || words.size() > 64)
        return analysisError(Issue::InvalidResponse, "Invalid word count in Dharmamitra analysis");

    Analysis result;
    for (const auto &item : words) {
        const auto word = item.toObject();
        const auto form = word.value("form").toString().simplified();
        const auto devanagari = word.value("devanagari").toString().simplified();
        const auto tag = word.value("tag");
        if (form.isEmpty() || devanagari.isEmpty() || !tag.isString())
            return analysisError(Issue::InvalidResponse, "Malformed word in Dharmamitra analysis");

        result.words.push_back(
            {form.toStdString(),
             devanagari.toStdString(),
             word.value("lemma").toString().simplified().toStdString(),
             word.value("slp1").toString().toStdString(),
             parseGrammar(tag.toString().simplified())});
    }

    result.notice = json.value("notice").toString().simplified().toStdString();
    return result;
}

QString schemeName(Script script)
{
    switch (script) {
    case Script::Devanagari:
        return "devanagari";
    case Script::Iast:
        return "iast";
    case Script::Slp1:
        return "slp1";
    }
    return {};
}

template<typename Operation>
Request handleFor(Operation *operation)
{
    return Request([operation = QPointer<Operation>(operation)] {
        if (operation)
            operation->cancel();
    });
}
} // namespace

detail::DictionaryLookup::DictionaryLookup(
    HttpClient &http,
    const SanskritEndpoints &endpoints,
    Dictionary dictionary,
    Completion<DictionaryResult> completion,
    QObject *parent)
    : QObject(parent)
    , http(http)
    , endpoints(endpoints)
    , completion(std::move(completion))
{
    result.dictionary = dictionary;
}

void detail::DictionaryLookup::start(const std::vector<std::string> &words, bool includeBaseForms)
{
    if (!completion)
        return;

    morphologyDone = !includeBaseForms;
    for (const auto &word : words) {
        const auto headword = QString::fromStdString(word);
        headwords.append(headword);
        lookup(headword);
    }

    if (includeBaseForms && !headwords.isEmpty())
        lookupBaseForms(headwords.first());
    else

        morphologyDone = true;
    finishIfReady();
}

void detail::DictionaryLookup::cancel()
{
    if (!completion)
        return;
    completion = {};
    for (auto *request : findChildren<HttpRequest *>())
        request->cancel();
    deleteLater();
}

void detail::DictionaryLookup::lookupBaseForms(const QString &word)
{
    QUrl url = endpoints.inflectedForms;
    QUrlQuery query;
    query.addQueryItem("word", word);
    query.addQueryItem("transLit", "SLP2SLP");
    query.addQueryItem("filter", "SLP2SLP");
    url.setQuery(query);

    auto *request = get(http, url, this);
    connect(request, &HttpRequest::finished, this, [this, request] {
        const auto &response = request->result();
        request->deleteLater();

        morphologyDone = true;
        if (!response.ok) {
            diagnose(Stage::Morphology, requestIssue(response), response.error);
        } else {
            const auto parsed = detail::parseInflectedForms(response.body);
            if (!parsed.ok)
                diagnose(Stage::Morphology, parseIssue(parsed), parsed.error);
            for (const auto &headword : parsed.values) {
                result.baseFormsSlp1.push_back(headword.toStdString());
                if (!headwords.contains(headword)) {
                    headwords.append(headword);
                    lookup(headword);
                }
            }
        }
        finishIfReady();
    });
}

QUrl detail::DictionaryLookup::dictionaryUrl(
    const QString &field, const QString &value, const QString &mode) const
{
    const QString dictionary = result.dictionary == Dictionary::Pwg ? "pwg" : "mw";
    QUrl url = endpoints.dictionaries.resolved(QUrl(dictionary + "/restful/entries"));
    QUrlQuery query;
    query.addQueryItem("field", field);
    query.addQueryItem("query", value);
    query.addQueryItem("query_type", mode);
    query.addQueryItem("size", "20");
    url.setQuery(query);
    return url;
}

void detail::DictionaryLookup::lookup(const QString &headword, bool subentry)
{
    ++outstanding;
    const QUrl url = dictionaryUrl(subentry ? "re_headwords_slp1" : "headword_slp1", headword);

    auto *request = get(http, url, this);
    connect(request, &HttpRequest::finished, this, [this, request, headword, subentry] {
        const auto &response = request->result();
        request->deleteLater();

        --outstanding;
        if (!response.ok) {
            diagnose(Stage::Dictionary, requestIssue(response), response.error, headword);
        } else {
            const auto parsed = subentry ? detail::parseSanskritSubentries(
                                               response.body, headword, result.dictionary)
                                         : detail::parseSanskritDictionary(
                                               response.body, headword, result.dictionary);
            if (!parsed.ok)
                diagnose(Stage::Dictionary, parseIssue(parsed), parsed.error, headword);

            directMeanings.insert(headword, meanings(parsed.values, headword));
            for (qint64 record : parsed.previousEntries)
                lookupPrevious(headword, record, 0);
            if (!subentry && result.dictionary == Dictionary::Mw && parsed.ok
                && parsed.values.isEmpty())
                lookup(headword, true);
        }
        finishIfReady();
    });
}

void detail::DictionaryLookup::lookupPrevious(const QString &headword, qint64 record, int depth)
{
    const QString key = headword + ':' + QString::number(record);
    if (requestedReferences.contains(key))
        return;
    if (depth >= 4 || requestedReferences.size() >= 16) {
        diagnose(Stage::Reference, Issue::Limit, "Dictionary reference limit reached", headword);
        return;
    }

    requestedReferences.insert(key);
    ++outstanding;
    // XML search can also match citations. The parser verifies the exact tail/L record.
    auto *request = get(http, dictionaryUrl("xml", QString::number(record), "match"), this);
    connect(request, &HttpRequest::finished, this, [this, request, headword, record, depth] {
        const auto &response = request->result();
        request->deleteLater();

        --outstanding;
        if (!response.ok) {
            diagnose(Stage::Reference, requestIssue(response), response.error, headword);
        } else {
            const auto parsed = detail::parseSanskritPreviousEntry(response.body, record);
            if (!parsed.ok)
                diagnose(Stage::Reference, parseIssue(parsed), parsed.error, headword);
            referenceMeanings[headword]
                .insert(record, meanings(parsed.values, parsed.sourceHeadword, record));
            for (qint64 previous : parsed.previousEntries)
                lookupPrevious(headword, previous, depth + 1);
        }
        finishIfReady();
    });
}

void detail::DictionaryLookup::diagnose(
    Stage stage, Issue issue, const QString &message, const QString &headword)
{
    result.diagnostics.push_back({stage, issue, message.toStdString(), headword.toStdString()});
}

void detail::DictionaryLookup::finishIfReady()
{
    if (!completion || !morphologyDone || outstanding)
        return;
    for (const auto &headword : headwords) {
        DictionaryEntry entry{headword.toStdString(), {}};
        const auto references = referenceMeanings.value(headword);
        for (const auto &glosses : references)
            entry.meanings.insert(entry.meanings.end(), glosses.begin(), glosses.end());
        const auto glosses = directMeanings.value(headword);
        entry.meanings.insert(entry.meanings.end(), glosses.begin(), glosses.end());
        result.entries.push_back(std::move(entry));
    }

    auto complete = std::exchange(completion, {});
    deleteLater();
    complete(std::move(result));
}

detail::AnalysisRequest::AnalysisRequest(
    JsonProcess &worker, Completion<Analysis> completion, QObject *parent)
    : QObject(parent)
    , worker(worker)
    , completion(std::move(completion))
{}

void detail::AnalysisRequest::start(std::string text, Script script)
{
    if (!worker.isConfigured()) {
        deliver(analysisError(Issue::NotConfigured, "Python interpreter is not configured"));
        return;
    }

    worker.call(
        "sanskrit.analyze",
        {{"text", QString::fromStdString(text)}, {"scheme", schemeName(script)}},
        this,
        [this](const QJsonObject &analysis, const QString &error) {
            deliver(
                error.isEmpty() ? parseAnalysis(analysis)
                                : analysisError(Issue::RequestFailed, error));
        });
}

void detail::AnalysisRequest::cancel()
{
    if (!completion)
        return;
    completion = {};
    worker.cancel(this);
    deleteLater();
}

void detail::AnalysisRequest::deliver(Analysis analysis)
{
    QTimer::singleShot(0, this, [this, analysis = std::move(analysis)]() mutable {
        if (auto complete = std::exchange(completion, {})) {
            deleteLater();
            complete(std::move(analysis));
        }
    });
}


SanskritServiceQt::State::State(const SanskritEndpoints &endpoints, const HttpOptions &options)
    : endpoints(endpoints)
    , http(options)
{}

SanskritServiceQt::SanskritServiceQt(const SanskritEndpoints &endpoints, const HttpOptions &options)
    : state(std::make_unique<State>(endpoints, options))
{}

SanskritServiceQt::~SanskritServiceQt() = default;

QString SanskritServiceQt::setPythonInterpreter(const QString &interpreter)
{
    if (interpreter.isEmpty()) {
        configureWorker({}, {});
        return {};
    }
    static const auto script = bundledDispatcher();
    if (script.isEmpty())
        return "Cannot read the bundled Python dispatcher";
    configureWorker(interpreter, {"-I", "-u", "-X", "utf8", "-c", script});
    return {};
}

void SanskritServiceQt::configureWorker(const QString &program, const QStringList &arguments)
{
    state->worker.configure(program, arguments);
}

void SanskritServiceQt::post(std::function<void()> task)
{
    QTimer::singleShot(0, &state->context, std::move(task));
}

Request SanskritServiceQt::lookup(
    std::vector<std::string> headwords,
    Dictionary dictionary,
    bool includeBaseForms,
    Completion<DictionaryResult> completion)
{
    auto *operation = new detail::DictionaryLookup(
        state->http, state->endpoints, dictionary, std::move(completion), &state->context);
    QTimer::singleShot(0, operation, [operation, headwords = std::move(headwords), includeBaseForms] {
        operation->start(headwords, includeBaseForms);
    });
    return handleFor(operation);
}

Request SanskritServiceQt::analyze(std::string text, Script script, Completion<Analysis> completion)
{
    auto *operation = new detail::AnalysisRequest(
        state->worker, std::move(completion), &state->context);
    operation->start(std::move(text), script);
    return handleFor(operation);
}
} // namespace sanskrit
