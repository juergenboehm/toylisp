#pragma once

#include "evaluators/evaluator.h"
#include "sanskrit/sanskritservice.h"

namespace sanskrit_evaluator_detail {
class SanskritEvaluationJob : public EvaluationJob
{
public:
    explicit SanskritEvaluationJob(QObject *parent);

    void setRequest(sanskrit::Request value);

private:
    sanskrit::Request request;
};
} // namespace sanskrit_evaluator_detail
