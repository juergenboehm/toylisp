#include "testevaluator.h"
#include "sanskrit/sanskrit.h"

Evaluation TestEvaluator::evaluate(const QString &input)
{
    using sanskrit::Sanskrit;
    using sanskrit::Script;
    const auto slp1
        = Sanskrit::transliterateWord(input.toStdString(), Script::Devanagari, Script::Slp1);
    if (!slp1.error.empty())
        return {false, QString::fromStdString(slp1.error)};
    const auto devanagari = Sanskrit::transliterateWord(slp1.text, Script::Slp1, Script::Devanagari);
    return {true, QString::fromStdString(slp1.text + '\n' + devanagari.text)};
}
