#include "testevaluator.h"
#include "evaluators/sanskrit/transliteration.h"

Evaluation TestEvaluator::evaluate(const QString &input)
{
    QString slp1;
    QString error;
    if (!devanagariToSlp1(input, slp1, error))
        return {false, error};

    return {true, slp1 + "\n" + slp1ToDevanagari(slp1)};
}
