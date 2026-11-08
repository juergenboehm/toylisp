#include "sanskrit.h"
#include "transliteration.h"
#include <memory>
#include <unordered_set>

namespace sanskrit {
namespace {
constexpr std::size_t maxAnalysisLength = 4000;
constexpr std::size_t maxHeadwords = 64;

template<typename Result>
Request deliverLater(SanskritService &service, Completion<Result> completion, Result result)
{
    auto pending = std::make_shared<bool>(true);
    service.post(
        [pending, completion = std::move(completion), result = std::move(result)]() mutable {
            if (std::exchange(*pending, false))
                completion(std::move(result));
        });
    return Request([pending] { *pending = false; });
}

Request invalidLookup(
    SanskritService &service,
    Dictionary dictionary,
    std::string error,
    Completion<DictionaryResult> completion)
{
    DictionaryResult result;
    result.dictionary = dictionary;
    result.diagnostics.push_back({Stage::Input, Issue::InvalidInput, std::move(error), {}});
    return deliverLater(service, std::move(completion), std::move(result));
}

struct GlossRequest
{
    Request stage;
    AnalysisWithGlosses result;
    Completion<AnalysisWithGlosses> completion;
};
} // namespace

Request Sanskrit::lookupDictionary(
    std::string_view word, LookupOptions options, Completion<DictionaryResult> completion) const
{
    auto converted = transliterateWord(word, options.script, Script::Slp1);
    if (!converted.error.empty())
        return invalidLookup(
            service, options.dictionary, std::move(converted.error), std::move(completion));

    return service.lookup(
        {std::move(converted.text)},
        options.dictionary,
        options.includeBaseForms,
        std::move(completion));
}

Request Sanskrit::lookupHeadwords(
    std::vector<std::string> slp1Headwords,
    Dictionary dictionary,
    Completion<DictionaryResult> completion) const
{
    if (slp1Headwords.size() > maxHeadwords)
        return invalidLookup(service, dictionary, "Enter at most 64 headwords", std::move(completion));

    std::unordered_set<std::string> seen;
    std::vector<std::string> headwords;
    for (auto &headword : slp1Headwords) {
        if (!detail::isSlp1Word(headword))
            return invalidLookup(service, dictionary, "Invalid SLP1 headword", std::move(completion));
        if (seen.insert(headword).second)
            headwords.push_back(std::move(headword));
    }
    if (headwords.empty()) {
        DictionaryResult result;
        result.dictionary = dictionary;
        return deliverLater(service, std::move(completion), std::move(result));
    }
    return service.lookup(std::move(headwords), dictionary, false, std::move(completion));
}

Request Sanskrit::analyze(std::string_view text, Script script, Completion<Analysis> completion) const
{
    std::string error;
    const auto normalized = detail::normalizedText(text, error);
    const auto input = detail::trimmed(normalized);
    // Preserve the existing input limit in UTF-16 code units, also for non-BMP text.
    std::size_t length = 0;
    for (const auto character : input)
        length += character > 0xffff ? 2 : 1;
    if (error.empty() && (input.empty() || length > maxAnalysisLength))
        error = "Enter Sanskrit text of at most 4000 characters";
    if (!error.empty()) {
        Analysis result;
        result.diagnostics.push_back({Stage::Input, Issue::InvalidInput, std::move(error), {}});
        return deliverLater(service, std::move(completion), std::move(result));
    }
    return service.analyze(detail::encode(input), script, std::move(completion));
}

Request Sanskrit::analyzeWithGlosses(
    std::string_view text,
    Script script,
    Dictionary dictionary,
    Completion<AnalysisWithGlosses> completion) const
{
    auto request = std::make_shared<GlossRequest>();
    request->result.dictionary.dictionary = dictionary;
    request->completion = std::move(completion);
    const std::weak_ptr<GlossRequest> pending = request;
    request->stage
        = analyze(text, script, [pending, backend = &service, dictionary](Analysis analysis) {
              const auto active = pending.lock();
              if (!active)
                  return;
              std::vector<std::string> lemmata;
              for (std::size_t i = 0; i < analysis.words.size(); ++i) {
                  const auto &headword = analysis.words[i].lemmaSlp1;
                  if (detail::isSlp1Word(headword))
                      lemmata.push_back(headword);
                  else
                      active->result.wordsWithoutHeadwords.push_back(i);
              }
              active->result.analysis = std::move(analysis);
              if (lemmata.empty()) {
                  auto complete = std::move(active->completion);
                  complete(std::move(active->result));
                  return;
              }
              active->stage = Sanskrit(*backend).lookupHeadwords(
                  std::move(lemmata), dictionary, [pending](DictionaryResult result) {
                      if (const auto active = pending.lock()) {
                          active->result.dictionary = std::move(result);
                          auto complete = std::move(active->completion);
                          complete(std::move(active->result));
                      }
                  });
          });
    return Request([request] { request->stage.cancel(); });
}
} // namespace sanskrit
