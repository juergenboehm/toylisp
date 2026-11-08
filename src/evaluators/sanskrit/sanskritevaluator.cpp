#include "sanskritevaluator.h"
#include "transliteration.h"
#include <QUrlQuery>

SanskritEvaluator::SanskritJob::SanskritJob(
    HttpClient &http,
    const SanskritEndpoints &endpoints,
    SanskritLanguage language,
    QObject *parent)
    : EvaluationJob(parent)
    , http(http)
    , endpoints(endpoints)
    , language(language)
{
    connect(this, &EvaluationJob::cancellationRequested, this, [this] {
        for (auto *request : findChildren<HttpRequest *>())
            request->cancel();
    });
}

void SanskritEvaluator::SanskritJob::startWord(const QString &word)
{
    headwords.append(word);
    lookup(word);
    QUrl url = endpoints.inflectedForms;
    QUrlQuery query;
    query.addQueryItem("word", word);
    query.addQueryItem("transLit", "SLP2SLP");
    query.addQueryItem("filter", "SLP2SLP");
    url.setQuery(query);
    auto *request = http.get(url, this);
    connect(request, &HttpRequest::finished, this, [this, request] {
        const auto response = request->result();
        request->deleteLater();
        morphologyDone = true;
        if (!response.ok) {
            errors.append("Inflected forms: " + response.error);
        } else {
            const auto parsed = parseInflectedForms(response.body);
            if (!parsed.ok) {
                errors.append("Inflected forms: " + parsed.error);
            } else {
                morphologyHeadwords = parsed.values;
                for (const auto &headword : parsed.values) {
                    if (!headwords.contains(headword)) {
                        headwords.append(headword);
                        lookup(headword);
                    }
                }
            }
        }
        finishIfReady();
    });
}

void SanskritEvaluator::SanskritJob::startHeadwords(const QList<SanskritHeadword> &lemmas)
{
    morphologyDone = true;
    for (const auto &lemma : lemmas) {
        headwords.append(lemma.slp1);
        headwordLabels.insert(lemma.slp1, lemma.label);
        lookup(lemma.slp1);
    }
    finishIfReady();
}

QUrl SanskritEvaluator::SanskritJob::dictionaryUrl(
    const QString &field, const QString &value, const QString &mode) const
{
    const QString dictionary = language == SanskritLanguage::German ? "pwg" : "mw";
    QUrl url = endpoints.dictionaries.resolved(QUrl(dictionary + "/restful/entries"));
    QUrlQuery query;
    query.addQueryItem("field", field);
    query.addQueryItem("query", value);
    query.addQueryItem("query_type", mode);
    query.addQueryItem("size", "20");
    url.setQuery(query);
    return url;
}

void SanskritEvaluator::SanskritJob::lookup(const QString &headword, bool subentry)
{
    ++outstanding;
    const QUrl url = dictionaryUrl(subentry ? "re_headwords_slp1" : "headword_slp1", headword);
    auto *request = http.get(url, this);
    connect(request, &HttpRequest::finished, this, [this, request, headword, subentry] {
        const auto response = request->result();
        request->deleteLater();
        --outstanding;
        const QString label = slp1ToDevanagari(headword) + " ("
                              + (language == SanskritLanguage::German ? "PWG" : "MW") + "): ";
        if (!response.ok) {
            errors.append(label + response.error);
        } else {
            const auto parsed = subentry
                                    ? parseSanskritSubentries(response.body, headword, language)
                                    : parseSanskritDictionary(response.body, headword, language);
            if (!parsed.ok)
                errors.append(label + parsed.error);
            meanings.insert(headword, parsed.values);
            for (qint64 record : parsed.previousEntries)
                lookupPrevious(headword, record, 0);
            if (!subentry && language == SanskritLanguage::English && parsed.ok
                && parsed.values.isEmpty())
                lookup(headword, true);
        }
        finishIfReady();
    });
}

void SanskritEvaluator::SanskritJob::lookupPrevious(
    const QString &headword, qint64 record, int depth)
{
    const QString key = headword + ':' + QString::number(record);
    if (requestedReferences.contains(key))
        return;
    const QString label = slp1ToDevanagari(headword) + " (PWG reference): ";
    if (depth >= 4 || requestedReferences.size() >= 16) {
        errors.append(label + "Dictionary reference limit reached");
        return;
    }
    requestedReferences.insert(key);
    ++outstanding;
    // XML full-text search can also match citations. The parser accepts
    // only the exact tail/L record, never an incidental occurrence.
    auto *request = http.get(dictionaryUrl("xml", QString::number(record), "match"), this);
    connect(request, &HttpRequest::finished, this, [this, request, headword, record, depth, label] {
        const auto response = request->result();
        request->deleteLater();
        --outstanding;
        if (!response.ok) {
            errors.append(label + response.error);
        } else {
            const auto parsed = parseSanskritPreviousEntry(response.body, record);
            if (!parsed.ok)
                errors.append(label + parsed.error);
            referenceMeanings[headword].insert(record, parsed.values);
            for (qint64 previous : parsed.previousEntries)
                lookupPrevious(headword, previous, depth + 1);
        }
        finishIfReady();
    });
}

void SanskritEvaluator::SanskritJob::finishIfReady()
{
    if (!morphologyDone || outstanding || isCancelled())
        return;
    QStringList lines;
    for (const auto &headword : headwords) {
        if (headwordLabels.contains(headword)) {
            const auto glosses = meanings.value(headword);
            QString gloss = glosses.mid(0, 2).join("; ");
            if (glosses.size() > 2)
                gloss += QString::fromUtf8(" …");
            lines.append("  " + headwordLabels.value(headword) + ": "
                         + (gloss.isEmpty() ? QString("No usable MW gloss") : gloss));
            continue;
        }
        for (const auto &reference : referenceMeanings.value(headword)) {
            for (const auto &meaning : reference)
                lines.append(slp1ToDevanagari(headword) + QString::fromUtf8(" — ") + meaning);
        }
        for (const auto &meaning : meanings.value(headword))
            lines.append(slp1ToDevanagari(headword) + QString::fromUtf8(" — ") + meaning);
    }
    const bool hasMeanings = !lines.isEmpty();
    if (!hasMeanings && !morphologyHeadwords.isEmpty()) {
        QStringList recognized;
        for (const auto &headword : morphologyHeadwords)
            recognized.append(slp1ToDevanagari(headword));
        lines.append("Recognized base form(s): " + recognized.join(", "));
    }
    if (!errors.isEmpty()) {
        lines.append("Lookup incomplete:");
        lines.append(errors);
    } else if (!hasMeanings) {
        if (morphologyHeadwords.isEmpty())
            lines.append(
                "No dictionary entry or supported base form found for "
                + slp1ToDevanagari(headwords.value(0)));
        else
            lines.append(
                QString("No dictionary meanings found in ")
                + (language == SanskritLanguage::German ? "PWG" : "MW"));
    }
    finish({errors.isEmpty(), lines.join('\n')});
}

SanskritEvaluator::SanskritEvaluator(const SanskritEndpoints &endpoints, const HttpOptions &options)
    : http(options)
    , endpoints(endpoints)
{}

EvaluationJob *SanskritEvaluator::startEvaluation(const QString &input, QObject *parent)
{
    QString word, error;
    if (!devanagariToSlp1(input, word, error)) {
        auto *job = new EvaluationJob(parent);
        job->finish({false, error});
        return job;
    }
    auto *job = new SanskritJob(http, endpoints, language, parent);
    job->startWord(word);
    return job;
}

EvaluationJob *SanskritEvaluator::startMonierWilliamsLookup(
    const QList<SanskritHeadword> &lemmas, QObject *parent)
{
    auto *job = new SanskritJob(http, endpoints, SanskritLanguage::English, parent);
    job->startHeadwords(lemmas);
    return job;
}
