#include "evaluator.h"
#include "arith/arithmeticevaluator.h"

EvaluationJob::EvaluationJob(QObject *parent)
    : QObject(parent)
{}

void EvaluationJob::finish(const Evaluation &result)
{
    if (finished || cancelled)
        return;
    evaluation = result;
    finished = true;
    emit completed();
}

void EvaluationJob::cancel()
{
    if (finished || cancelled)
        return;
    cancelled = true;
    emit cancellationRequested();
}

Evaluation Evaluator::evaluate(const QString &)
{
    return {false, "This evaluator requires asynchronous evaluation"};
}

EvaluationJob *Evaluator::startEvaluation(const QString &input, QObject *parent)
{
    auto *job = new EvaluationJob(parent);
    job->finish(evaluate(input));
    return job;
}

EvaluatorRegistry::EvaluatorRegistry()
{
    evaluators.emplace("arith", std::make_unique<ArithmeticEvaluator>());
}
Evaluation EvaluatorRegistry::evaluate(const QString &name, const QString &input)
{
    QString selected = name.isEmpty() ? defaultName : name;
    auto found = evaluators.find(selected);
    if (found == evaluators.end())
        return {false, "Unknown evaluator: " + selected};
    return found->second->evaluate(input);
}

EvaluationJob *EvaluatorRegistry::startEvaluation(
    const QString &name, const QString &input, QObject *parent)
{
    const QString selected = name.isEmpty() ? defaultName : name;
    auto found = evaluators.find(selected);
    if (found != evaluators.end())
        return found->second->startEvaluation(input, parent);
    auto *job = new EvaluationJob(parent);
    job->finish({false, "Unknown evaluator: " + selected});
    return job;
}

void EvaluatorRegistry::addEvaluator(const QString &name, std::unique_ptr<Evaluator> evaluator)
{
    // Registration happens during application setup; existing session owners stay intact.
    evaluators.emplace(name, std::move(evaluator));
}
