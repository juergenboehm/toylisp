#pragma once

#include <QMap>
#include <QString>

namespace arithmetic_detail {
class Parser
{
    QString source;
    int position = 0;
    const QMap<QString, double> &variables;

    void spaces();

    bool take(QChar c);

    [[noreturn]] void fail(const QString &message);

    double primary(int depth);

    double term(int depth);

    double expression(int depth);

public:
    Parser(QString text, const QMap<QString, double> &environment);

    double parse();
};
} // namespace arithmetic_detail
