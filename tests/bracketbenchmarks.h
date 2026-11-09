#pragma once

#include <QObject>

class BracketBenchmarks : public QObject
{
    Q_OBJECT

private slots:
    void matching_data();

    void matching();
};
