#pragma once
#include <map>
#include <memory>
#include <QMap>
#include <QString>

struct Evaluation
{
    bool ok;
    QString text;
};
class Evaluator
{
public:
    virtual ~Evaluator() = default;
    virtual Evaluation evaluate(const QString &input) = 0;
};
class ArithmeticEvaluator : public Evaluator
{
public:
    Evaluation evaluate(const QString &input) override;

private:
    QMap<QString, double> variables;
};
class EvaluatorRegistry
{
public:
    EvaluatorRegistry();
    QString defaultName = "arith";
    Evaluation evaluate(const QString &name, const QString &input);

private:
    std::map<QString, std::unique_ptr<Evaluator>> evaluators;
};
