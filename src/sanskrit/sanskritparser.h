#pragma once

#include "sanskrittypes.h"
#include <QByteArray>
#include <QList>
#include <QStringList>

namespace sanskrit::detail {

struct LookupParseResult
{
    bool ok = false;
    QStringList values;
    QString error;
    QList<qint64> previousEntries;
    QString sourceHeadword;
    bool limitExceeded = false;
};

LookupParseResult parseInflectedForms(const QByteArray &html);

LookupParseResult parseSanskritDictionary(
    const QByteArray &json, const QString &headword, Dictionary dictionary);

LookupParseResult parseSanskritSubentries(
    const QByteArray &json, const QString &headword, Dictionary dictionary);

LookupParseResult parseSanskritPreviousEntry(const QByteArray &json, qint64 recordNumber);
} // namespace sanskrit::detail
