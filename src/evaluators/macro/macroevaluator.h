#pragma once

#include "evaluators/evaluator.h"
#include <QPointer>

class PythonMacroService;

class MacroEvaluator : public Evaluator
{
public:
    explicit MacroEvaluator(PythonMacroService &service);

    void setMacro(const QString &path);

    EvaluationJob *startEvaluation(const QString &input, QObject *parent) override;

private:
    QPointer<PythonMacroService> service;
    QString path;
};
