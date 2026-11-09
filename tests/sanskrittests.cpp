#include "sanskrittests.h"
#include "sanskrit/sanskrit.h"
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace sanskrit;

void check(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

namespace sanskrit_test {
using namespace sanskrit;

void TestService::post(std::function<void()> task)
{
    tasks.push_back(std::move(task));
}

Request TestService::lookup(
    std::vector<std::string> headwords,
    Dictionary dictionary,
    bool includeBaseForms,
    Completion<DictionaryResult> completion)
{
    ++lookups;
    lastHeadwords = std::move(headwords);
    lastIncludeBaseForms = includeBaseForms;
    auto result = dictionaryResult;
    result.dictionary = dictionary;
    return enqueue(std::move(completion), std::move(result));
}

Request TestService::analyze(std::string text, Script script, Completion<Analysis> completion)
{
    ++analyses;
    lastText = std::move(text);
    lastScript = script;
    return enqueue(std::move(completion), analysis);
}

void TestService::runNext()
{
    check(!tasks.empty(), "expected a pending callback");
    auto task = std::move(tasks.front());
    tasks.pop_front();
    task();
}

template<typename Result>
Request TestService::enqueue(Completion<Result> completion, Result result)
{
    auto cancelled = std::make_shared<bool>(false);
    post([cancelled, completion = std::move(completion), result = std::move(result)]() mutable {
        if (!*cancelled)
            completion(std::move(result));
    });
    return Request([cancelled] { *cancelled = true; });
}
} // namespace sanskrit_test

namespace {
using sanskrit_test::TestService;

void transliteration()
{
    const auto iast
        = Sanskrit::transliterate("रामः, कृष्णः।\nगच्छति 😀", Script::Devanagari, Script::Iast);
    check(
        iast.error.empty() && iast.text == "rāmaḥ, kṛṣṇaḥ।\ngacchati 😀",
        "Devanagari text to IAST with separators");
    const auto slp1 = Sanskrit::transliterate(iast.text, Script::Iast, Script::Slp1);
    check(slp1.error.empty() && slp1.text == "rAmaH, kfzRaH।\ngacCati 😀", "IAST text to SLP1");
    check(
        Sanskrit::transliterate(slp1.text, Script::Slp1, Script::Devanagari).text
            == "रामः, कृष्णः।\nगच्छति 😀",
        "SLP1 text to Devanagari");
    check(
        Sanskrit::transliterate("'Rāma', so'ham.", Script::Iast, Script::Devanagari).text
            == "'राम', सोऽहम्.",
        "quoted IAST and avagraha");
    check(
        Sanskrit::transliterate("अँ ~ अ", Script::Devanagari, Script::Iast).text == "a~ ~ a",
        "IAST candrabindu and separated punctuation");
    check(
        Sanskrit::transliterate("a~ ~ a", Script::Iast, Script::Devanagari).text == "अँ ~ अ",
        "read IAST candrabindu only within text");
    check(
        Sanskrit::transliterateWord("ra\u0304ma", Script::Iast, Script::Slp1).text == "rAma",
        "decomposed IAST long vowel");
    check(
        Sanskrit::transliterateWord("Kr\u0323s\u0323n\u0323a", Script::Iast, Script::Devanagari).text
            == "कृष्ण",
        "decomposed IAST consonants and vocalic r");
    check(
        Sanskrit::transliterateWord("दूरतस्", Script::Devanagari, Script::Slp1).text == "dUratas",
        "word-final virama");
    check(
        !Sanskrit::transliterateWord("राम कृष्ण", Script::Devanagari, Script::Slp1).error.empty(),
        "reject multiple words in word mode");
    check(
        !Sanskrit::transliterate("\xc0\xaf", Script::Devanagari, Script::Iast).error.empty(),
        "reject malformed UTF-8");
}

void lookup()
{
    TestService service;
    Sanskrit sanskrit(service);
    int completed = 0;
    auto invalid = sanskrit.lookupDictionary("agni", {}, [&](DictionaryResult result) {
        check(
            result.diagnostics.size() == 1
                && result.diagnostics.front().issue == Issue::InvalidInput,
            "structured input diagnostic");
        ++completed;
    });
    check(service.lookups == 0 && completed == 0, "invalid input is asynchronous and local");
    service.runNext();
    check(completed == 1, "invalid input completes once");

    auto request = sanskrit.lookupDictionary(
        "agni", {Dictionary::Mw, Script::Iast, false}, [&](DictionaryResult result) {
            check(
                result.dictionary == Dictionary::Mw && result.diagnostics.empty(),
                "exact lookup retains dictionary selection");
            ++completed;
        });
    check(
        service.lastHeadwords == std::vector<std::string>{"agni"} && !service.lastIncludeBaseForms,
        "pass normalized headword and exact search option");
    service.runNext();
    check(completed == 2, "valid lookup completes");
    {
        auto dropped = sanskrit.lookupDictionary("राम", {}, [&](DictionaryResult) { ++completed; });
        check(
            service.lastHeadwords == std::vector<std::string>{"rAma"}
                && service.lastIncludeBaseForms,
            "enable possible base forms by default");
    }
    service.runNext();
    check(completed == 2, "dropping a request cancels delivery");
}

void analysisWithGlosses()
{
    TestService service;
    Sanskrit sanskrit(service);
    service.analysis.words = {
        {"te", "ते", "tvad", "tvad", {{"Case", "Genitive"}}},
        {"tvam", "त्वम्", "tvad", "tvad", {}},
        {"ca", "च", "ca", {}, {}},
    };
    service.dictionaryResult.entries = {{"tvad", {{"thou", "tvad", {}}}}};
    service.dictionaryResult.diagnostics = {
        {Stage::Dictionary, Issue::RequestFailed, "Partial result", "tvad"}};

    int completed = 0;
    auto request = sanskrit.analyzeWithGlosses(
        "  te tvam ca\n", Script::Iast, Dictionary::Mw, [&](AnalysisWithGlosses result) {
            check(
                result.analysis.words.size() == 3
                    && result.analysis.words.front().grammar.front().value == "Genitive",
                "retain analysis and grammar");
            check(
                result.wordsWithoutHeadwords == std::vector<std::size_t>{2},
                "mark unavailable lemma");
            check(
                result.dictionary.entries.front().meanings.front().text == "thou"
                    && result.dictionary.diagnostics.size() == 1,
                "retain glosses with partial diagnostics");
            ++completed;
        });
    check(
        service.analyses == 1 && service.lastText == "te tvam ca"
            && service.lastScript == Script::Iast,
        "pass trimmed text and explicit script");
    service.runNext();
    check(
        service.lastHeadwords == std::vector<std::string>{"tvad"} && !service.lastIncludeBaseForms,
        "deduplicate and use exact lemmata in combined lookup");
    check(completed == 0, "combined operation awaits dictionary");
    service.runNext();
    check(completed == 1, "combined operation delivers once");

    auto cancelled
        = sanskrit.analyzeWithGlosses("te", Script::Iast, Dictionary::Mw, [&](AnalysisWithGlosses) {
              ++completed;
          });
    cancelled.cancel();
    service.runNext();
    check(service.lookups == 1 && completed == 1, "cancel before dictionary prevents follow-up");

    cancelled
        = sanskrit.analyzeWithGlosses("te", Script::Iast, Dictionary::Mw, [&](AnalysisWithGlosses) {
              ++completed;
          });
    service.runNext();
    cancelled.cancel();
    service.runNext();
    check(
        service.lookups == 2 && completed == 1,
        "cancel in the dictionary stage suppresses delivery");
}
} // namespace

int main()
{
    try {
        transliteration();
        lookup();
        analysisWithGlosses();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
