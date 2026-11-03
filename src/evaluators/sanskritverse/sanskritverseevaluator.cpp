#include "sanskritverseevaluator.h"
#include "evaluators/sanskrit/sanskritevaluator.h"
#include "evaluators/sanskrit/transliteration.h"
#include "processes/jsonprocess.h"
#include <QFile>
#include <QJsonArray>
#include <QMap>
#include <QSet>

namespace {
QString compactGrammar(const QString &tag)
{
    if (tag.isEmpty())
        return "Unspecified";
    static const QMap<QString, QString> abbreviations{
        {"Nominative", "Nom"}, {"Vocative", "Voc"}, {"Accusative", "Acc"},
        {"Instrumental", "Ins"}, {"Dative", "Dat"}, {"Ablative", "Abl"},
        {"Genitive", "Gen"}, {"Locative", "Loc"}, {"Singular", "Sg"}, {"Dual", "Du"},
        {"Plural", "Pl"}, {"Masculine", "m"}, {"Feminine", "f"}, {"Neuter", "n"},
        {"First", "1p"}, {"Second", "2p"}, {"Third", "3p"}};
    QMap<QString, QString> features;
    QStringList extra;
    for (const auto &item : tag.split(',')) {
        const int equals = item.indexOf('=');
        const QString value = item.mid(equals + 1).trimmed();
        if (equals < 0 || !abbreviations.contains(value))
            extra.append(item.trimmed());
        else
            features.insert(item.left(equals).trimmed(), abbreviations.value(value));
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

void finishVerse(EvaluationJob *job, SanskritEvaluator &dictionary, const QJsonObject &analysis)
{
    const auto words = analysis.value("words").toArray();
    if (words.isEmpty() || words.size() > 64) {
        job->finish({false, "Invalid word count in Dharmamitra analysis"});
        return;
    }
    QStringList pada;
    QStringList grammar;
    QList<SanskritHeadword> headwords;
    QSet<QString> requested;
    QStringList unavailable;
    for (const auto &item : words) {
        const auto word = item.toObject();
        const QString form = word.value("form").toString().simplified();
        const QString devanagari = word.value("devanagari").toString().simplified();
        const QString lemma = word.value("lemma").toString().simplified();
        const auto tag = word.value("tag");
        if (form.isEmpty() || devanagari.isEmpty() || !tag.isString()) {
            job->finish({false, "Malformed word in Dharmamitra analysis"});
            return;
        }
        pada.append(devanagari);
        grammar.append(form + " (" + compactGrammar(tag.toString().simplified()) + ")");

        const QString slp1 = word.value("slp1").toString();
        if (!isSlp1Word(slp1)) {
            unavailable.append(devanagari);
        } else if (!requested.contains(slp1)) {
            requested.insert(slp1);
            const auto label = form == lemma ? lemma : lemma + QString::fromUtf8(" → ") + form;
            headwords.append({slp1, slp1ToDevanagari(slp1) + " (" + label + ")"});
        }
    }
    QString text = QString::fromUtf8("Padapāṭha: ") + pada.join(" | ")
                   + "\nGrammatik: " + grammar.join(" | ");
    const auto notice = analysis.value("notice").toString().simplified();
    if (!notice.isEmpty())
        text += "\nDharmamitra: " + notice;
    text += "\nMW-Einträge:\n";
    if (!unavailable.isEmpty()) {
        unavailable.removeDuplicates();
        text += "  No usable dictionary headword for: " + unavailable.join(", ") + '\n';
    }
    if (headwords.isEmpty()) {
        job->finish({true, text.trimmed()});
        return;
    }
    auto *lookup = dictionary.startMonierWilliamsLookup(headwords, job);
    QObject::connect(job, &EvaluationJob::cancellationRequested, lookup, &EvaluationJob::cancel);
    QObject::connect(lookup, &EvaluationJob::completed, job, [job, lookup, text] {
        const auto result = lookup->result();
        job->finish({result.ok, text + result.text});
    });
}
} // namespace

SanskritVerseEvaluator::SanskritVerseEvaluator(JsonProcess &worker, SanskritEvaluator &dictionary)
    : worker(worker)
    , dictionary(dictionary)
{}

EvaluationJob *SanskritVerseEvaluator::startEvaluation(const QString &input, QObject *parent)
{
    auto *job = new EvaluationJob(parent);
    const auto text = input.trimmed();
    if (text.isEmpty() || text.size() > 4000) {
        job->finish({false, "Enter Sanskrit text of at most 4000 characters"});
        return job;
    }
    if (!worker.isConfigured()) {
        job->finish({false, "Set a Python interpreter under Sanskrit → Python Interpreter…"});
        return job;
    }
    QObject::connect(job, &EvaluationJob::cancellationRequested, &worker,
                     [this, job] { worker.cancel(job); });
    worker.call("sanskrit.analyze", {{"text", text}}, job,
                [this, job](const QJsonObject &analysis, const QString &error) {
                    if (error.isEmpty()) {
                        finishVerse(job, dictionary, analysis);
                    } else {
                        job->finish({false, "Sanskrit verse analysis: " + error});
                    }
                });
    return job;
}

QString configurePythonDispatcher(JsonProcess &worker, const QString &interpreter)
{
    if (interpreter.isEmpty()) {
        worker.configure({}, {});
        return {};
    }
    Q_INIT_RESOURCE(python);
    QFile source(":/python/dispatcher.py");
    if (!source.open(QIODevice::ReadOnly))
        return "Cannot read the bundled Python dispatcher";
    const auto script = QString::fromUtf8(source.readAll());
    if (script.isEmpty() || source.error() != QFileDevice::NoError)
        return "Cannot read the bundled Python dispatcher";
    worker.configure(interpreter, {"-I", "-u", "-X", "utf8", "-c", script});
    return {};
}
