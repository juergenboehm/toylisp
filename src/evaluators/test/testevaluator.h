#pragma once
#include "evaluators/evaluator.h"

class TestEvaluator : public Evaluator
{
public:
    Evaluation evaluate(const QString &input) override;
};
