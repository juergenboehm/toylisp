#include "sanskritparser.h"
#include "transliteration.h"
#include <vector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace {
struct TeiNode
{
    QString name;
    QString text;
    QMap<QString, QString> attributes;
    std::vector<TeiNode> children;
};

TeiNode readNode(QXmlStreamReader &xml, int depth, int &remainingNodes)
{
    TeiNode node;
    if (depth > 64 || --remainingNodes < 0) {
        xml.raiseError("XML structure exceeds the size limit");
        return node;
    }
    node.name = xml.name().toString();
    for (const auto &attribute : xml.attributes())
        node.attributes.insert(attribute.qualifiedName().toString(), attribute.value().toString());
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            node.children.push_back(readNode(xml, depth + 1, remainingNodes));
        } else if (xml.isCharacters()) {
            if (--remainingNodes < 0) {
                xml.raiseError("XML structure exceeds the size limit");
                break;
            }
            node.children.push_back({{}, xml.text().toString(), {}, {}});
        } else if (xml.isEndElement()) {
            break;
        } else if (xml.isDTD() || xml.isEntityReference()) {
            xml.raiseError("Unsupported XML entity or document type");
        }
    }
    return node;
}

bool literaryCitation(const TeiNode &node)
{
    return node.name == "cit" && node.attributes.value("type") == "literary_source";
}

QString visibleText(const TeiNode &node, bool sanskrit = false)
{
    if (node.name.isEmpty())
        return sanskrit ? slp1ToDevanagari(node.text) : node.text;
    if (node.name == "note" || node.name == "gramGrp" || node.name == "re" || literaryCitation(node)
        || (node.name == "ref" && node.attributes.value("type") == "facs"))
        return {};
    if (node.name == "choice") {
        for (const auto &child : node.children) {
            if (child.attributes.value("ana") == "as1")
                return visibleText(child);
        }
        return node.children.empty() ? QString() : visibleText(node.children.front());
    }
    if (node.name == "g")
        return node.attributes.value("ref").contains("root") ? QString::fromUtf8("√") : QString();
    sanskrit = sanskrit || node.attributes.value("xml:lang").startsWith("san-Latn-x-SLP1");
    QString result;
    for (const auto &child : node.children)
        result += visibleText(child, sanskrit);
    return result;
}

QString englishGloss(const TeiNode &sense)
{
    size_t start = 0;
    for (size_t i = 0; i < sense.children.size(); ++i) {
        const auto &child = sense.children[i];
        if (child.name == "milestone"
            && child.attributes.value("unit") == "following_word_part_of_inf") {
            // Verb paradigms precede the first infinitive gloss in MW.
            start = i + 1;
            break;
        }
    }
    QString text;
    int brackets = 0;
    bool skipLeadingBrackets = false;
    for (size_t i = start; i < sense.children.size(); ++i) {
        const auto &child = sense.children[i];
        if (child.name == "sense" || child.name == "re")
            break;
        const QString unit = child.attributes.value("unit");
        if (child.name == "pc" && (unit.endsWith("-left") || unit.endsWith("-right"))) {
            if (unit.endsWith("-left")) {
                if (brackets == 0 && text.trimmed().isEmpty())
                    skipLeadingBrackets = true;
                ++brackets;
            } else {
                brackets = qMax(0, brackets - 1);
                if (skipLeadingBrackets && brackets == 0) {
                    skipLeadingBrackets = false;
                    continue;
                }
            }
        }
        if (skipLeadingBrackets)
            continue;
        if (literaryCitation(child) && brackets == 0 && !text.trimmed().isEmpty())
            break;
        text += visibleText(child);
    }
    return text.simplified();
}

bool isSenseLabel(const TeiNode &node)
{
    const QString number = node.attributes.value("n");
    return node.name == "sense" && !number.isEmpty() && visibleText(node).simplified() == number;
}

bool usesPreviousDefinition(const TeiNode &sense)
{
    QString preamble;
    for (const auto &child : sense.children) {
        if (child.name.isEmpty())
            preamble += child.text;
        else if (child.name != "gramGrp")
            break;
    }
    preamble = preamble.simplified();
    return QRegularExpression("^dass\\.(?:\\s|:|$)").match(preamble).hasMatch();
}

QString germanGloss(const TeiNode &sense, bool &hasInlineSubsense)
{
    QString result;
    bool collecting = false;
    for (const auto &child : sense.children) {
        // PWG sometimes closes an initial sub-sense after its label alone;
        // its definition then follows as siblings in the containing sense.
        if (!collecting && isSenseLabel(child)) {
            hasInlineSubsense = true;
            continue;
        }
        if (child.name == "sense")
            break;
        if (child.name == "hi" && child.attributes.value("rendition") == "#i") {
            result += visibleText(child);
            collecting = true;
        } else if (collecting) {
            if (!child.name.isEmpty())
                break; // The first Sanskrit example or citation ends the leading gloss.
            result += child.text;
        }
    }
    return result.simplified();
}

void collectGlosses(
    const TeiNode &sense,
    SanskritLanguage language,
    QStringList &glosses,
    bool &usesPrevious,
    bool allowPreviousDefinition = false)
{
    if (allowPreviousDefinition && sense.attributes.value("n").isEmpty()
        && language == SanskritLanguage::German && usesPreviousDefinition(sense)) {
        usesPrevious = true;
        return; // The following Sanskrit quotation illustrates the inherited definition.
    }
    bool hasInlineSubsense = false;
    const QString gloss = language == SanskritLanguage::German
                              ? germanGloss(sense, hasInlineSubsense)
                              : englishGloss(sense);
    if (!gloss.isEmpty() && !glosses.contains(gloss))
        glosses.append(gloss);
    if (language == SanskritLanguage::German && !gloss.isEmpty()) {
        // Later lettered siblings can complete the inline sub-sense group.
        // Other nested PWG material contains examples and derived forms.
        if (hasInlineSubsense) {
            for (const auto &child : sense.children) {
                if (child.name == "sense" && child.attributes.value("ana") == "e"
                    && !isSenseLabel(child))
                    collectGlosses(child, language, glosses, usesPrevious);
            }
        }
        return;
    }
    int previousNumber = 0;
    for (const auto &child : sense.children) {
        if (child.name != "sense")
            continue;
        const auto number = QRegularExpression("^(\\d+)").match(child.attributes.value("n"));
        if (number.hasMatch()) {
            const int currentNumber = number.captured(1).toInt();
            // PWG also packs later derivations/prefixed verbs into the root entry.
            // A restarted enumeration belongs to that later material.
            if (previousNumber && currentNumber <= previousNumber)
                break;
            previousNumber = currentNumber;
        }
        collectGlosses(child, language, glosses, usesPrevious);
    }
}

QString rawText(const TeiNode &node)
{
    QString result = node.text;
    for (const auto &child : node.children)
        result += rawText(child);
    return result;
}

QString entryHeadword(const TeiNode &entry)
{
    for (const auto &form : entry.children) {
        if (form.name != "form")
            continue;
        for (const auto &orth : form.children) {
            if (orth.name == "orth" && orth.attributes.value("ana") == "key1")
                return rawText(orth).trimmed();
        }
    }
    return {};
}

void findRecordNumbers(const TeiNode &node, QList<qint64> &numbers)
{
    if (node.name == "re")
        return;
    if (node.name == "note" && node.attributes.value("ana") == "tail") {
        for (const auto &child : node.children) {
            if (child.name != "idno" || child.attributes.value("ana") != "L")
                continue;
            bool ok = false;
            const qint64 number = rawText(child).trimmed().toLongLong(&ok);
            if (ok && number > 0 && !numbers.contains(number))
                numbers.append(number);
        }
    }
    for (const auto &child : node.children)
        findRecordNumbers(child, numbers);
}

qint64 recordNumber(const TeiNode &entry)
{
    QList<qint64> numbers;
    findRecordNumbers(entry, numbers);
    return numbers.size() == 1 ? numbers.first() : 0;
}

void findSubentries(
    const TeiNode &node, const QString &headword, std::vector<const TeiNode *> &matches)
{
    if (node.name == "re" && entryHeadword(node) == headword)
        matches.push_back(&node);
    for (const auto &child : node.children)
        findSubentries(child, headword, matches);
}
} // namespace

LookupParseResult parseInflectedForms(const QByteArray &html)
{
    const QString text = QString::fromUtf8(html);
    const int separator = text.lastIndexOf("<disp1>");
    if (separator < 0)
        return {false, {}, "Unexpected inflected-forms response"};
    // Read only the provider's summary links, never scripts or rendered HTML.
    const QString summary = text.mid(separator + 7);
    const QRegularExpression link("id_info\\('([^']+)'\\)");
    QStringList words;
    auto matches = link.globalMatch(summary);
    while (matches.hasNext()) {
        QString url = matches.next().captured(1);
        url.replace("&amp;", "&");
        const QString word = QUrlQuery(QUrl(url)).queryItemValue("key");
        if (!isSlp1Word(word))
            return {false, {}, "Invalid headword in inflected-forms response"};
        if (!words.contains(word))
            words.append(word);
    }
    if (words.isEmpty() && !summary.trimmed().isEmpty())
        return {false, {}, "Unexpected inflected-forms summary format"};
    if (words.size() > 16)
        return {false, {}, "More than 16 possible base forms; narrow the input"};
    return {true, words, {}};
}

namespace {
enum class EntrySelection { Headword, Subentry, PreviousRecord };

LookupParseResult parseDictionary(
    const QByteArray &json,
    const QString &headword,
    SanskritLanguage language,
    EntrySelection selection,
    qint64 expectedRecord = 0)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {false, {}, "Invalid dictionary JSON"};
    const auto data = document.object().value("data");
    if (!data.isObject() || !data.toObject().value("entries").isArray())
        return {false, {}, "Missing dictionary entries array"};
    const auto entries = data.toObject().value("entries").toArray();
    if (entries.size() >= 20)
        return {false, {}, "Dictionary result limit reached; results may be incomplete"};
    QStringList glosses;
    QStringList errors;
    QList<qint64> previousEntries;
    int matchingRecords = 0;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const QString returnedHeadword = entry.value("headword_slp1").toString();
        if (!value.isObject() || !isSlp1Word(returnedHeadword)
            || (selection == EntrySelection::Headword && returnedHeadword != headword)
            || !entry.value("xml").isString()) {
            errors.append("Unexpected dictionary entry format");
            continue;
        }
        QXmlStreamReader xml(entry.value("xml").toString());
        TeiNode root;
        int remainingNodes = 100000;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isDTD() || xml.isEntityReference())
                xml.raiseError("Unsupported XML entity or document type");
            if (xml.isStartElement()) {
                if (!root.name.isEmpty() || xml.name() != "entry"
                    || xml.namespaceUri() != "http://www.tei-c.org/ns/1.0") {
                    xml.raiseError("Expected a TEI dictionary entry");
                    break;
                }
                root = readNode(xml, 0, remainingNodes);
            }
        }
        if (xml.hasError() || root.name.isEmpty()) {
            errors.append("Invalid dictionary XML: " + xml.errorString());
            continue;
        }
        std::vector<const TeiNode *> selected;
        if (selection == EntrySelection::Headword) {
            selected.push_back(&root);
        } else {
            if (entryHeadword(root) != returnedHeadword) {
                errors.append("Dictionary headword does not match its XML");
                continue;
            }
            if (selection == EntrySelection::Subentry) {
                findSubentries(root, headword, selected);
                if (selected.empty())
                    errors.append("No matching dictionary subentry");
            } else if (recordNumber(root) == expectedRecord) {
                ++matchingRecords;
                selected.push_back(&root);
            }
        }
        if (selected.empty())
            continue;
        QStringList entryGlosses;
        bool usesPrevious = false;
        for (const auto *selectedEntry : selected) {
            for (const auto &child : selectedEntry->children) {
                if (child.name == "sense")
                    collectGlosses(child, language, entryGlosses, usesPrevious, true);
            }
        }
        if (usesPrevious) {
            const qint64 number = recordNumber(root);
            if (number <= 1)
                errors.append("Cannot resolve PWG 'dass.' without a preceding record number");
            else if (!previousEntries.contains(number - 1))
                previousEntries.append(number - 1);
        }
        if (entryGlosses.isEmpty() && !usesPrevious) {
            errors.append("No supported meaning structure in dictionary entry");
            continue;
        }
        for (auto gloss : entryGlosses) {
            if (selection == EntrySelection::PreviousRecord)
                gloss = "wie " + slp1ToDevanagari(returnedHeadword) + ": " + gloss;
            if (!glosses.contains(gloss))
                glosses.append(gloss);
        }
    }
    if (selection == EntrySelection::PreviousRecord) {
        if (!matchingRecords)
            errors.append("Referenced PWG record not found");
        else if (matchingRecords > 1) {
            errors.append("Ambiguous PWG record number");
            glosses.clear();
            previousEntries.clear();
        }
    }
    errors.removeDuplicates();
    return {errors.isEmpty(), glosses, errors.join("; "), previousEntries};
}
} // namespace

LookupParseResult parseSanskritDictionary(
    const QByteArray &json, const QString &headword, SanskritLanguage language)
{
    return parseDictionary(json, headword, language, EntrySelection::Headword);
}

LookupParseResult parseSanskritSubentries(
    const QByteArray &json, const QString &headword, SanskritLanguage language)
{
    return parseDictionary(json, headword, language, EntrySelection::Subentry);
}

LookupParseResult parseSanskritPreviousEntry(const QByteArray &json, qint64 expectedRecord)
{
    if (expectedRecord <= 0)
        return {false, {}, "Invalid PWG record number"};
    return parseDictionary(
        json, {}, SanskritLanguage::German, EntrySelection::PreviousRecord, expectedRecord);
}
