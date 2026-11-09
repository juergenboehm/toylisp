#pragma once

#include "evaluators/evaluator.h"
#include <QMap>

class ArithmeticEvaluator : public Evaluator
{
public:
    Evaluation evaluate(const QString &input) override;

private:
    QMap<QString, double> variables;
};
