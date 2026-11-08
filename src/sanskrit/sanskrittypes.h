#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sanskrit {
enum class Script { Devanagari, Iast, Slp1 };
enum class Dictionary { Pwg, Mw };

struct Transliteration
{
    std::string text;
    std::string error;
};

enum class Issue { InvalidInput, NotConfigured, RequestFailed, InvalidResponse, Limit };
enum class Stage { Input, Morphology, Dictionary, Reference, Analysis };

struct Diagnostic
{
    Stage stage;
    Issue issue;
    std::string message;
    std::string headwordSlp1;
};

struct Meaning
{
    std::string text;
    std::string sourceHeadwordSlp1;
    std::optional<std::int64_t> sourceRecord;
};

struct DictionaryEntry
{
    std::string headwordSlp1;
    std::vector<Meaning> meanings;
};

struct DictionaryResult
{
    Dictionary dictionary = Dictionary::Pwg;
    std::vector<DictionaryEntry> entries;
    std::vector<std::string> baseFormsSlp1;
    std::vector<Diagnostic> diagnostics;
};

struct GrammarFeature
{
    std::string name;
    std::optional<std::string> value;
};

struct AnalyzedWord
{
    std::string formIast;
    std::string formDevanagari;
    std::string lemmaIast;
    std::string lemmaSlp1;
    std::vector<GrammarFeature> grammar;
};

struct Analysis
{
    std::vector<AnalyzedWord> words;
    std::string notice;
    std::vector<Diagnostic> diagnostics;
};

struct AnalysisWithGlosses
{
    Analysis analysis;
    DictionaryResult dictionary;
    std::vector<std::size_t> wordsWithoutHeadwords;
};

struct LookupOptions
{
    Dictionary dictionary = Dictionary::Pwg;
    Script script = Script::Devanagari;
    bool includeBaseForms = true;
};
} // namespace sanskrit
