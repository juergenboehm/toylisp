#pragma once

#include "sanskritservice.h"
#include <string_view>

namespace sanskrit {
class Sanskrit
{
public:
    explicit Sanskrit(SanskritService &service)
        : service(service)
    {}

    // Text is UTF-8, in the specified script. Preserve separators and punctuation.
    static Transliteration transliterate(std::string_view text, Script from, Script to);
    // Validate a single Sanskrit word in the supported classical repertoire.
    static Transliteration transliterateWord(std::string_view word, Script from, Script to);

    Request lookupDictionary(
        std::string_view word, LookupOptions options, Completion<DictionaryResult> completion) const;
    Request lookupHeadwords(
        std::vector<std::string> slp1Headwords,
        Dictionary dictionary,
        Completion<DictionaryResult> completion) const;
    Request analyze(std::string_view text, Script script, Completion<Analysis> completion) const;
    Request analyzeWithGlosses(
        std::string_view text,
        Script script,
        Dictionary dictionary,
        Completion<AnalysisWithGlosses> completion) const;

private:
    SanskritService &service;
};
} // namespace sanskrit
