#pragma once
#include <map>
#include <memory>
#include <QMap>
#include <QObject>
#include <QString>

struct Evaluation
{
    bool ok;
    QString text;
};
class EvaluationJob : public QObject
{
    Q_OBJECT
public:
    explicit EvaluationJob(QObject *parent = nullptr);
    bool isFinished() const { return finished; }
    bool isCancelled() const { return cancelled; }
    Evaluation result() const { return evaluation; }
    void finish(const Evaluation &result);
    void cancel();
signals:
    void completed();
    void cancellationRequested();

private:
    bool finished = false;
    bool cancelled = false;
    Evaluation evaluation{false, {}};
};
class Evaluator
{
public:
    virtual ~Evaluator() = default;
    virtual Evaluation evaluate(const QString &input);
    virtual EvaluationJob *startEvaluation(const QString &input, QObject *parent);
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
    EvaluationJob *startEvaluation(const QString &name, const QString &input, QObject *parent);
    void addEvaluator(const QString &name, std::unique_ptr<Evaluator> evaluator);

private:
    std::map<QString, std::unique_ptr<Evaluator>> evaluators;
};
