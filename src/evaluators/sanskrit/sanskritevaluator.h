#pragma once

#include "evaluators/evaluator.h"
#include "sanskrit/sanskrittypes.h"

namespace sanskrit {
class Sanskrit;
}

class SanskritEvaluator : public Evaluator
{
public:
    explicit SanskritEvaluator(sanskrit::Sanskrit &sanskrit);
    void setDictionary(sanskrit::Dictionary value) { dictionary = value; }
    EvaluationJob *startEvaluation(const QString &input, QObject *parent) override;

private:
    sanskrit::Sanskrit &sanskrit;
    sanskrit::Dictionary dictionary = sanskrit::Dictionary::Pwg;
};

class SanskritVerseEvaluator : public Evaluator
{
public:
    explicit SanskritVerseEvaluator(sanskrit::Sanskrit &sanskrit);
    EvaluationJob *startEvaluation(const QString &input, QObject *parent) override;

private:
    sanskrit::Sanskrit &sanskrit;
};
