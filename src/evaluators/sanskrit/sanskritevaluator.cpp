#include "sanskritevaluator.h"
#include "sanskrit/sanskrit.h"
#include <QMap>
#include <QStringList>

namespace {
using namespace sanskrit;

class SanskritEvaluationJob : public EvaluationJob
{
public:
    explicit SanskritEvaluationJob(QObject *parent)
        : EvaluationJob(parent)
    {
        connect(this, &EvaluationJob::cancellationRequested, this, [this] { request.cancel(); });
    }

    void setRequest(Request value) { request = std::move(value); }

private:
    Request request;
};

QString devanagari(const std::string &headword)
{
    return QString::fromStdString(
        Sanskrit::transliterateWord(headword, Script::Slp1, Script::Devanagari).text);
}

QString dictionaryName(Dictionary dictionary)
{
    return dictionary == Dictionary::Pwg ? "PWG" : "MW";
}

QString diagnosticText(const Diagnostic &diagnostic, Dictionary dictionary)
{
    const auto message = QString::fromStdString(diagnostic.message);
    switch (diagnostic.stage) {
    case Stage::Morphology:
        return "Inflected forms: " + message;
    case Stage::Dictionary:
        return devanagari(diagnostic.headwordSlp1) + " (" + dictionaryName(dictionary)
               + "): " + message;
    case Stage::Reference:
        return devanagari(diagnostic.headwordSlp1) + " (PWG reference): " + message;
    case Stage::Analysis:
        if (diagnostic.issue == Issue::NotConfigured)
            return "Set a Python interpreter under Sanskrit → Python Interpreter…";
        return "Sanskrit verse analysis: " + message;
    case Stage::Input:
        return message;
    }
    return message;
}

void appendLookupDiagnostics(QStringList &lines, const DictionaryResult &result)
{
    if (!result.diagnostics.empty()) {
        lines.append("Lookup incomplete:");
        for (const auto &diagnostic : result.diagnostics)
            lines.append(diagnosticText(diagnostic, result.dictionary));
    }
}

QString meaningText(const Meaning &meaning)
{
    auto text = QString::fromStdString(meaning.text);
    if (meaning.sourceRecord)
        text.prepend("wie " + devanagari(meaning.sourceHeadwordSlp1) + ": ");
    return text;
}

Evaluation formatLookup(const DictionaryResult &result)
{
    if (!result.diagnostics.empty() && result.diagnostics.front().stage == Stage::Input)
        return {false, diagnosticText(result.diagnostics.front(), result.dictionary)};

    QStringList lines;
    for (const auto &entry : result.entries) {
        for (const auto &meaning : entry.meanings)
            lines.append(devanagari(entry.headwordSlp1) + " — " + meaningText(meaning));
    }
    const bool hasMeanings = !lines.isEmpty();
    if (!hasMeanings && !result.baseFormsSlp1.empty()) {
        QStringList recognized;
        for (const auto &headword : result.baseFormsSlp1)
            recognized.append(devanagari(headword));
        lines.append("Recognized base form(s): " + recognized.join(", "));
    }
    if (!result.diagnostics.empty()) {
        appendLookupDiagnostics(lines, result);
    } else if (!hasMeanings) {
        if (result.baseFormsSlp1.empty()) {
            const auto headword = result.entries.empty()
                                      ? QString{}
                                      : devanagari(result.entries.front().headwordSlp1);
            lines.append("No dictionary entry or supported base form found for " + headword);
        } else {
            lines.append("No dictionary meanings found in " + dictionaryName(result.dictionary));
        }
    }
    return {result.diagnostics.empty(), lines.join('\n')};
}

QString compactGrammar(const std::vector<GrammarFeature> &grammar)
{
    if (grammar.empty())
        return "Unspecified";
    static const QMap<QString, QString> abbreviations{
        {"Nominative", "Nom"},
        {"Vocative", "Voc"},
        {"Accusative", "Acc"},
        {"Instrumental", "Ins"},
        {"Dative", "Dat"},
        {"Ablative", "Abl"},
        {"Genitive", "Gen"},
        {"Locative", "Loc"},
        {"Singular", "Sg"},
        {"Dual", "Du"},
        {"Plural", "Pl"},
        {"Masculine", "m"},
        {"Feminine", "f"},
        {"Neuter", "n"},
        {"First", "1p"},
        {"Second", "2p"},
        {"Third", "3p"}};
    QMap<QString, QString> features;
    QStringList extra;
    for (const auto &feature : grammar) {
        const auto name = QString::fromStdString(feature.name);
        if (!feature.value) {
            extra.append(name);
            continue;
        }
        const auto value = QString::fromStdString(*feature.value);
        if (abbreviations.contains(value))
            features.insert(name, abbreviations.value(value));
        else
            extra.append(name + '=' + value);
    }
    QStringList parts;
    for (const auto &key : {"Case", "Number", "Gender", "Person"}) {
        const auto value = features.take(key);
        if (!value.isEmpty())
            parts.append(value);
    }
    parts += features.values();
    const auto compact = parts.join('.');
    if (extra.isEmpty())
        return compact;
    return compact.isEmpty() ? extra.join(", ") : compact + ", " + extra.join(", ");
}

Evaluation formatVerse(const AnalysisWithGlosses &result)
{
    const auto &analysis = result.analysis;
    const auto &dictionary = result.dictionary;
    QStringList lines;
    if (analysis.words.empty()) {
        for (const auto &diagnostic : analysis.diagnostics)
            lines.append(diagnosticText(diagnostic, dictionary.dictionary));
        return {false, lines.join('\n')};
    }

    QStringList pada;
    QStringList grammar;
    QMap<std::string, QString> labels;
    for (const auto &word : analysis.words) {
        pada.append(QString::fromStdString(word.formDevanagari));
        const auto form = QString::fromStdString(word.formIast);
        grammar.append(form + " (" + compactGrammar(word.grammar) + ")");
        if (!labels.contains(word.lemmaSlp1)) {
            const auto lemma = QString::fromStdString(word.lemmaIast);
            const auto label = form == lemma ? lemma : lemma + " → " + form;
            labels.insert(word.lemmaSlp1, devanagari(word.lemmaSlp1) + " (" + label + ")");
        }
    }
    lines.append("Padapāṭha: " + pada.join(" | "));
    lines.append("Grammatik: " + grammar.join(" | "));
    if (!analysis.notice.empty())
        lines.append("Dharmamitra: " + QString::fromStdString(analysis.notice));
    for (const auto &diagnostic : analysis.diagnostics)
        lines.append(diagnosticText(diagnostic, dictionary.dictionary));
    lines.append("MW-Einträge:");
    if (!result.wordsWithoutHeadwords.empty()) {
        QStringList unavailable;
        for (const auto index : result.wordsWithoutHeadwords)
            unavailable.append(QString::fromStdString(analysis.words[index].formDevanagari));
        unavailable.removeDuplicates();
        lines.append("  No usable dictionary headword for: " + unavailable.join(", "));
    }
    for (const auto &entry : dictionary.entries) {
        QStringList glosses;
        for (std::size_t i = 0; i < entry.meanings.size() && i < 2; ++i)
            glosses.append(meaningText(entry.meanings[i]));
        auto gloss = glosses.join("; ");
        if (entry.meanings.size() > 2)
            gloss += " …";
        lines.append(
            "  " + labels.value(entry.headwordSlp1) + ": "
            + (gloss.isEmpty() ? QString("No usable MW gloss") : gloss));
    }
    appendLookupDiagnostics(lines, dictionary);
    return {analysis.diagnostics.empty() && dictionary.diagnostics.empty(), lines.join('\n')};
}
} // namespace

SanskritEvaluator::SanskritEvaluator(Sanskrit &sanskrit)
    : sanskrit(sanskrit)
{}

EvaluationJob *SanskritEvaluator::startEvaluation(const QString &input, QObject *parent)
{
    auto *job = new SanskritEvaluationJob(parent);
    job->setRequest(
        sanskrit.lookupDictionary(input.toStdString(), {dictionary}, [job](DictionaryResult result) {
            job->finish(formatLookup(result));
        }));
    return job;
}

SanskritVerseEvaluator::SanskritVerseEvaluator(Sanskrit &sanskrit)
    : sanskrit(sanskrit)
{}

EvaluationJob *SanskritVerseEvaluator::startEvaluation(const QString &input, QObject *parent)
{
    auto *job = new SanskritEvaluationJob(parent);
    job->setRequest(sanskrit.analyzeWithGlosses(
        input.toStdString(), Script::Devanagari, Dictionary::Mw, [job](AnalysisWithGlosses result) {
            job->finish(formatVerse(result));
        }));
    return job;
}
