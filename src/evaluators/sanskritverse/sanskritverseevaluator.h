#pragma once
#include "evaluators/evaluator.h"

class JsonProcess;
class SanskritEvaluator;

class SanskritVerseEvaluator : public Evaluator
{
public:
    SanskritVerseEvaluator(JsonProcess &worker, SanskritEvaluator &dictionary);
    EvaluationJob *startEvaluation(const QString &input, QObject *parent) override;

private:
    JsonProcess &worker;
    SanskritEvaluator &dictionary;
};

QString configurePythonDispatcher(JsonProcess &worker, const QString &interpreter);
