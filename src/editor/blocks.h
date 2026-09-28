#pragma once
#include <QString>
struct EvaluationBlock {
    bool valid = false;
    QString error;
    QString input;
    QString evaluator;
    int insertion = 0;
    int replacementEnd = 0;
};
EvaluationBlock locateBlock(const QString &document, int markerPosition);
int matchingBracket(const QString &text, int position);
