#pragma once
#include <QString>

// Strict single-word Devanagari input; SLP1 is internal to the lookup providers.
bool devanagariToSlp1(const QString &input, QString &output, QString &error);
QString slp1ToDevanagari(const QString &input);
bool isSlp1Word(const QString &input);
