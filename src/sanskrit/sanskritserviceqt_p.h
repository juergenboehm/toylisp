#pragma once

#include "processes/jsonprocess.h"
#include "sanskritserviceqt.h"
#include <QMap>
#include <QSet>

namespace sanskrit {
struct SanskritServiceQt::State
{
    State(const SanskritEndpoints &endpoints, const HttpOptions &options);

    SanskritEndpoints endpoints;
    HttpClient http;
    JsonProcess worker;
    QObject context;
};

namespace detail {
class DictionaryLookup : public QObject
{
public:
    DictionaryLookup(
        HttpClient &http,
        const SanskritEndpoints &endpoints,
        Dictionary dictionary,
        Completion<DictionaryResult> completion,
        QObject *parent);

    void start(const std::vector<std::string> &words, bool includeBaseForms);

    void cancel();

private:
    void lookupBaseForms(const QString &word);

    QUrl dictionaryUrl(
        const QString &field, const QString &value, const QString &mode = "term") const;

    void lookup(const QString &headword, bool subentry = false);

    void lookupPrevious(const QString &headword, qint64 record, int depth);

    void diagnose(Stage stage, Issue issue, const QString &message, const QString &headword = {});

    void finishIfReady();

    HttpClient &http;
    SanskritEndpoints endpoints;
    Completion<DictionaryResult> completion;
    DictionaryResult result;
    QStringList headwords;
    QMap<QString, std::vector<Meaning>> directMeanings;
    QMap<QString, QMap<qint64, std::vector<Meaning>>> referenceMeanings;
    QSet<QString> requestedReferences;
    bool morphologyDone = false;
    int outstanding = 0;
};

class AnalysisRequest : public QObject
{
public:
    AnalysisRequest(JsonProcess &worker, Completion<Analysis> completion, QObject *parent);

    void start(std::string text, Script script);

    void cancel();

private:
    void deliver(Analysis analysis);

    JsonProcess &worker;
    Completion<Analysis> completion;
};
} // namespace detail
} // namespace sanskrit
