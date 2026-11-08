#pragma once

#include "lookups/httpclient.h"
#include "sanskritservice.h"
#include <memory>
#include <QStringList>

namespace sanskrit {
struct SanskritEndpoints
{
    QUrl dictionaries{"https://api.c-salt.uni-koeln.de/dicts/"};
    QUrl inflectedForms{
        "https://www.sanskrit-lexicon.uni-koeln.de/scans/csl-inflect/web/getWord.php"};
};

class SanskritServiceQt final : public SanskritService
{
public:
    explicit SanskritServiceQt(
        const SanskritEndpoints &endpoints = {}, const HttpOptions &options = {});
    ~SanskritServiceQt() override;

    // Use the bundled dispatcher with the selected interpreter, or disable it with an empty path.
    QString setPythonInterpreter(const QString &interpreter);
    // Configure an alternate worker implementing the Sanskrit JSON-line protocol.
    void configureWorker(const QString &program, const QStringList &arguments);

    void post(std::function<void()> task) override;
    Request lookup(
        std::vector<std::string> headwords,
        Dictionary dictionary,
        bool includeBaseForms,
        Completion<DictionaryResult> completion) override;
    Request analyze(std::string text, Script script, Completion<Analysis> completion) override;

private:
    struct State;
    std::unique_ptr<State> state;
};
} // namespace sanskrit
