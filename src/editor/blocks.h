#pragma once
#include <QString>
#include <QtGui/QTextBlock>
struct EvaluationBlock
{
    bool valid = false;
    QString error;
    QString input;
    QString evaluator;
    int insertion = 0;
    int replacementEnd = 0;
};
EvaluationBlock locateBlock(const QTextBlock &markerBlock);
