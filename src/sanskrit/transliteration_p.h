#pragma once

#include "sanskrittypes.h"
#include <string_view>
#include <vector>

namespace sanskrit::detail {
enum class Kind { Vowel, Consonant, Mark };

struct Letter
{
    Kind kind;
    char slp1;
    std::u32string_view iast;
    char32_t devanagari;
    char32_t vowelSign = 0;
};

struct Token
{
    const Letter *letter;
    char32_t literal = 0;
};

std::vector<Token> tokenize(std::u32string_view input, Script script, bool wordOnly);

std::string render(const std::vector<Token> &tokens, Script script);

bool isWord(const std::vector<Token> &tokens, Script script);
} // namespace sanskrit::detail
