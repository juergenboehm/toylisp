#pragma once

#include <vector>
#include <QMap>
#include <QString>

namespace sanskrit::detail {
struct TeiNode
{
    QString name;
    QString text;
    QMap<QString, QString> attributes;
    std::vector<TeiNode> children;
};

enum class EntrySelection { Headword, Subentry, PreviousRecord };
} // namespace sanskrit::detail
