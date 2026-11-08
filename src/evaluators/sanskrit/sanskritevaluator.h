#pragma once
#include "evaluators/evaluator.h"
#include "lookups/httpclient.h"
#include "sanskritparser.h"
#include <QList>
#include <QMap>
#include <QSet>
#include <QStringList>

struct SanskritEndpoints
{
    QUrl dictionaries{"https://api.c-salt.uni-koeln.de/dicts/"};
    QUrl inflectedForms{
        "https://www.sanskrit-lexicon.uni-koeln.de/scans/csl-inflect/web/getWord.php"};
};

struct SanskritHeadword
{
    QString slp1;
    QString label;
};

class SanskritEvaluator : public Evaluator
{
public:
    explicit SanskritEvaluator(
        const SanskritEndpoints &endpoints = {}, const HttpOptions &options = {});
    void setLanguage(SanskritLanguage value) { language = value; }
    SanskritLanguage resultLanguage() const { return language; }
    EvaluationJob *startEvaluation(const QString &input, QObject *parent) override;
    EvaluationJob *startMonierWilliamsLookup(const QList<SanskritHeadword> &lemmas, QObject *parent);

private:
    class SanskritJob;

    HttpClient http;
    SanskritEndpoints endpoints;
    SanskritLanguage language = SanskritLanguage::German;
};

class SanskritEvaluator::SanskritJob : public EvaluationJob
{
public:
    SanskritJob(
        HttpClient &http,
        const SanskritEndpoints &endpoints,
        SanskritLanguage language,
        QObject *parent);
    void startWord(const QString &word);
    void startHeadwords(const QList<SanskritHeadword> &lemmas);

private:
    QUrl dictionaryUrl(
        const QString &field, const QString &value, const QString &mode = "term") const;
    void lookup(const QString &headword, bool subentry = false);
    void lookupPrevious(const QString &headword, qint64 record, int depth);
    void finishIfReady();

    HttpClient &http;
    SanskritEndpoints endpoints;
    SanskritLanguage language;
    QStringList headwords;
    QMap<QString, QString> headwordLabels;
    QStringList morphologyHeadwords;
    QMap<QString, QStringList> meanings;
    QMap<QString, QMap<qint64, QStringList>> referenceMeanings;
    QSet<QString> requestedReferences;
    QStringList errors;
    bool morphologyDone = false;
    int outstanding = 0;
};
