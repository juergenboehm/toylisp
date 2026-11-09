#include "arithmeticevaluator.h"
#include "arithmeticevaluator_p.h"
#include <cmath>
#include <stdexcept>
#include <QRegularExpression>

arithmetic_detail::Parser::Parser(QString text, const QMap<QString, double> &environment)
    : source(std::move(text))
    , variables(environment)
{}

void arithmetic_detail::Parser::spaces()
{
    while (position < source.size() && source[position].isSpace())
        ++position;
}

bool arithmetic_detail::Parser::take(QChar c)
{
    spaces();
    if (position < source.size() && source[position] == c) {
        ++position;
        return true;
    }
    return false;
}

[[noreturn]] void arithmetic_detail::Parser::fail(const QString &message)
{
    throw std::runtime_error(message.toStdString());
}

double arithmetic_detail::Parser::primary(int depth)
{
    if (depth > 256)
        fail("Expression nesting is too deep");
    if (take('-'))
        return -primary(depth + 1);
    if (take('+'))
        return primary(depth + 1);

    if (take('(')) {
        double value = expression(depth + 1);
        if (!take(')'))
            fail("Expected closing parenthesis");
        return value;
    }

    spaces();
    auto rest = source.mid(position);
    static const QRegularExpression number(
        "^(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");
    auto match = number.match(rest);
    if (match.hasMatch()) {
        position += match.capturedLength();
        bool ok;
        double value = match.captured().toDouble(&ok);
        if (!ok || !std::isfinite(value))
            fail("Number out of range");
        return value;
    }

    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z_0-9]*");
    match = identifier.match(rest);
    if (match.hasMatch()) {
        position += match.capturedLength();
        if (!variables.contains(match.captured()))
            fail("Unknown variable: " + match.captured());
        return variables.value(match.captured());
    }

    fail("Expected number, variable or parenthesized expression");
}

double arithmetic_detail::Parser::term(int depth)
{
    double value = primary(depth);
    while (true) {
        if (take('*'))
            value *= primary(depth);
        else if (take('/')) {
            double divisor = primary(depth);
            if (divisor == 0)
                fail("Division by zero");
            value /= divisor;
        } else
            return value;
    }
}

double arithmetic_detail::Parser::expression(int depth)
{
    double value = term(depth);
    while (true) {
        if (take('+'))
            value += term(depth);
        else if (take('-'))
            value -= term(depth);
        else
            return value;
    }
}

double arithmetic_detail::Parser::parse()
{
    double value = expression(0);

    spaces();
    if (position != source.size())
        fail("Unexpected trailing input");
    if (!std::isfinite(value))
        fail("Result out of range");
    return value;
}

Evaluation ArithmeticEvaluator::evaluate(const QString &input)
{
    auto pending = variables;
    QString result;

    const auto lines = input.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i].trimmed();
        if (line.isEmpty())
            continue;

        try {
            QString name;
            int assignment = line.indexOf('=');
            if (assignment >= 0) {
                name = line.left(assignment).trimmed();
                static const QRegularExpression identifier("^[A-Za-z_][A-Za-z_0-9]*$");
                if (!identifier.match(name).hasMatch())
                    throw std::runtime_error("Invalid variable name");
                line = line.mid(assignment + 1);
            }

            double value = arithmetic_detail::Parser(line, pending).parse();
            if (!name.isEmpty())
                pending[name] = value;
            result = QString::number(value == 0 ? 0 : value, 'g', 15);
        } catch (const std::exception &error) {
            return {
                false,
                QString("Error in line %1: %2").arg(i + 1).arg(QString::fromUtf8(error.what()))};
        }
    }

    variables = pending;
    return {true, result};
}
