#pragma once
#include "evaluators/evaluator.h"
#include "lookups/httpclient.h"
#include "sanskritparser.h"

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
    HttpClient http;
    SanskritEndpoints endpoints;
    SanskritLanguage language = SanskritLanguage::German;
};
