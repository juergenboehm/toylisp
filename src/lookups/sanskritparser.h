#pragma once
#include <QByteArray>
#include <QList>
#include <QStringList>

enum class SanskritLanguage { German, English };

struct LookupParseResult
{
    bool ok = false;
    QStringList values;
    QString error;
    QList<qint64> previousEntries;
};

LookupParseResult parseInflectedForms(const QByteArray &html);
LookupParseResult parseSanskritDictionary(
    const QByteArray &json, const QString &headword, SanskritLanguage language);
LookupParseResult parseSanskritSubentries(
    const QByteArray &json, const QString &headword, SanskritLanguage language);
LookupParseResult parseSanskritPreviousEntry(const QByteArray &json, qint64 recordNumber);
