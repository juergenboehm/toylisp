#include "transliteration.h"
#include "sanskrit.h"
#include <cstdlib>
#include <memory>
#include <utf8proc.h>

namespace sanskrit {
namespace {
enum class Kind { Vowel, Consonant, Mark };

struct Letter
{
    Kind kind;
    char slp1;
    std::u32string_view iast;
    char32_t devanagari;
    char32_t vowelSign = 0;
};

constexpr Letter letters[] = {
    {Kind::Vowel, 'a', U"a", U'अ'},       {Kind::Vowel, 'A', U"ā", U'आ', U'ा'},
    {Kind::Vowel, 'i', U"i", U'इ', U'ि'}, {Kind::Vowel, 'I', U"ī", U'ई', U'ी'},
    {Kind::Vowel, 'u', U"u", U'उ', U'ु'},  {Kind::Vowel, 'U', U"ū", U'ऊ', U'ू'},
    {Kind::Vowel, 'f', U"ṛ", U'ऋ', U'ृ'},  {Kind::Vowel, 'F', U"ṝ", U'ॠ', U'ॄ'},
    {Kind::Vowel, 'x', U"ḷ", U'ऌ', U'ॢ'},  {Kind::Vowel, 'X', U"ḹ", U'ॡ', U'ॣ'},
    {Kind::Vowel, 'e', U"e", U'ए', U'े'},  {Kind::Vowel, 'E', U"ai", U'ऐ', U'ै'},
    {Kind::Vowel, 'o', U"o", U'ओ', U'ो'}, {Kind::Vowel, 'O', U"au", U'औ', U'ौ'},
    {Kind::Consonant, 'k', U"k", U'क'},   {Kind::Consonant, 'K', U"kh", U'ख'},
    {Kind::Consonant, 'g', U"g", U'ग'},   {Kind::Consonant, 'G', U"gh", U'घ'},
    {Kind::Consonant, 'N', U"ṅ", U'ङ'},   {Kind::Consonant, 'c', U"c", U'च'},
    {Kind::Consonant, 'C', U"ch", U'छ'},  {Kind::Consonant, 'j', U"j", U'ज'},
    {Kind::Consonant, 'J', U"jh", U'झ'},  {Kind::Consonant, 'Y', U"ñ", U'ञ'},
    {Kind::Consonant, 'w', U"ṭ", U'ट'},   {Kind::Consonant, 'W', U"ṭh", U'ठ'},
    {Kind::Consonant, 'q', U"ḍ", U'ड'},   {Kind::Consonant, 'Q', U"ḍh", U'ढ'},
    {Kind::Consonant, 'R', U"ṇ", U'ण'},   {Kind::Consonant, 't', U"t", U'त'},
    {Kind::Consonant, 'T', U"th", U'थ'},  {Kind::Consonant, 'd', U"d", U'द'},
    {Kind::Consonant, 'D', U"dh", U'ध'},  {Kind::Consonant, 'n', U"n", U'न'},
    {Kind::Consonant, 'p', U"p", U'प'},   {Kind::Consonant, 'P', U"ph", U'फ'},
    {Kind::Consonant, 'b', U"b", U'ब'},   {Kind::Consonant, 'B', U"bh", U'भ'},
    {Kind::Consonant, 'm', U"m", U'म'},   {Kind::Consonant, 'y', U"y", U'य'},
    {Kind::Consonant, 'r', U"r", U'र'},   {Kind::Consonant, 'l', U"l", U'ल'},
    {Kind::Consonant, 'v', U"v", U'व'},   {Kind::Consonant, 'S', U"ś", U'श'},
    {Kind::Consonant, 'z', U"ṣ", U'ष'},   {Kind::Consonant, 's', U"s", U'स'},
    {Kind::Consonant, 'h', U"h", U'ह'},   {Kind::Mark, 'M', U"ṃ", U'ं'},
    {Kind::Mark, 'H', U"ḥ", U'ः'},        {Kind::Mark, '~', U"~", U'ँ'},
    {Kind::Mark, '\'', U"'", U'ऽ'},
};
constexpr char32_t virama = U'्';

struct Token
{
    const Letter *letter;
    char32_t literal = 0;
};

const Letter *findSlp1(char32_t character)
{
    for (const auto &letter : letters) {
        if (character == static_cast<char32_t>(letter.slp1))
            return &letter;
    }
    return nullptr;
}

const Letter *findIast(std::u32string_view text)
{
    const auto first = static_cast<char32_t>(utf8proc_tolower(text.front()));
    const auto second = text.size() > 1 ? static_cast<char32_t>(utf8proc_tolower(text[1])) : 0;
    const Letter *match = nullptr;
    for (const auto &letter : letters) {
        if (text.size() < letter.iast.size() || (match && match->iast.size() >= letter.iast.size()))
            continue;
        if (letter.iast.front() == first && (letter.iast.size() == 1 || letter.iast[1] == second))
            match = &letter;
    }
    return match;
}

const Letter *findDevanagari(char32_t character)
{
    for (const auto &letter : letters) {
        if (letter.devanagari == character)
            return &letter;
    }
    return nullptr;
}

const Letter *inherentOrExplicitVowel(std::u32string_view input, std::size_t &position)
{
    const auto next = position + 1 < input.size() ? input[position + 1] : U'\0';
    if (next == virama) {
        ++position;
        if (position + 1 < input.size()
            && (input[position + 1] == U'\u200c' || input[position + 1] == U'\u200d'))
            ++position;
        return nullptr;
    }
    if (next != U'\0') {
        for (const auto &letter : letters) {
            if (letter.vowelSign == next) {
                ++position;
                return &letter;
            }
        }
    }
    return &letters[0];
}

std::vector<Token> tokenize(std::u32string_view input, Script script, bool wordOnly)
{
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto character = input[i];
        const Letter *letter = nullptr;
        switch (script) {
        case Script::Devanagari:
            letter = findDevanagari(character);
            break;
        case Script::Iast:
            letter = findIast(input.substr(i));
            break;
        case Script::Slp1:
            letter = findSlp1(character);
            break;
        }
        // In Latin text an apostrophe between letters denotes avagraha.
        // Apostrophes at the edge of a word can instead be quotation marks.
        if (!wordOnly && script != Script::Devanagari && character == U'\'') {
            const auto before = tokens.empty() ? nullptr : tokens.back().letter;
            const auto after = i + 1 == input.size()
                                   ? nullptr
                                   : (script == Script::Slp1 ? findSlp1(input[i + 1])
                                                             : findIast(input.substr(i + 1)));
            if (!before || !after || after->kind == Kind::Mark)
                letter = nullptr;
        }
        if (!wordOnly && script != Script::Devanagari && character == U'~'
            && (tokens.empty() || !tokens.back().letter))
            letter = nullptr;
        if (!letter) {
            tokens.push_back({nullptr, character});
            continue;
        }
        tokens.push_back({letter});
        if (script == Script::Devanagari && letter->kind == Kind::Consonant) {
            if (const auto *vowel = inherentOrExplicitVowel(input, i))
                tokens.push_back({vowel});
        } else if (script == Script::Iast) {
            i += letter->iast.size() - 1;
        }
    }
    return tokens;
}

std::string render(const std::vector<Token> &tokens, Script script)
{
    std::u32string output;
    bool pendingConsonant = false;
    for (const auto &token : tokens) {
        const auto *letter = token.letter;
        if (script == Script::Devanagari) {
            if (pendingConsonant && letter && letter->kind == Kind::Vowel) {
                if (letter->vowelSign)
                    output += letter->vowelSign;
                pendingConsonant = false;
                continue;
            }
            if (pendingConsonant)
                output += virama;
            output += letter ? letter->devanagari : token.literal;
            pendingConsonant = letter && letter->kind == Kind::Consonant;
        } else if (!letter) {
            output += token.literal;
        } else if (script == Script::Iast) {
            output += letter->iast;
        } else {
            output += letter->slp1;
        }
    }
    if (pendingConsonant)
        output += virama;
    return detail::encode(output);
}

bool isWord(const std::vector<Token> &tokens, Script script)
{
    bool hasLetter = false;
    for (const auto &token : tokens) {
        if (!token.letter
            || (script == Script::Devanagari && !hasLetter && token.letter->kind == Kind::Mark))
            return false;
        hasLetter = hasLetter || token.letter->kind != Kind::Mark;
    }
    return hasLetter;
}

bool isSpace(char32_t character)
{
    const auto category = utf8proc_category(character);
    return category == UTF8PROC_CATEGORY_ZS || category == UTF8PROC_CATEGORY_ZL
           || category == UTF8PROC_CATEGORY_ZP || (character >= U'\t' && character <= U'\r')
           || character == 0x85;
}
} // namespace

namespace detail {
bool isSlp1Word(std::string_view word)
{
    if (word.empty() || word.size() > 128)
        return false;
    bool hasLetter = false;
    for (unsigned char character : word) {
        const auto *letter = findSlp1(character);
        if (!letter)
            return false;
        hasLetter = hasLetter || letter->kind != Kind::Mark;
    }
    return hasLetter;
}

std::string encode(std::u32string_view text)
{
    std::string result;
    for (const auto character : text) {
        utf8proc_uint8_t bytes[4];
        const auto length = utf8proc_encode_char(character, bytes);
        result.append(reinterpret_cast<const char *>(bytes), length);
    }
    return result;
}

std::u32string normalizedText(std::string_view text, std::string &error)
{
    if (text.empty())
        return {};
    utf8proc_uint8_t *buffer = nullptr;
    const auto length = utf8proc_map(
        reinterpret_cast<const utf8proc_uint8_t *>(text.data()),
        text.size(),
        &buffer,
        static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE));
    std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> normalized(buffer, std::free);
    if (length < 0) {
        error = utf8proc_errmsg(length);
        return {};
    }
    std::u32string result;
    for (utf8proc_ssize_t offset = 0; offset < length;) {
        utf8proc_int32_t character;
        // utf8proc_map already validated the entire buffer.
        offset += utf8proc_iterate(buffer + offset, length - offset, &character);
        result += static_cast<char32_t>(character);
    }
    return result;
}

std::u32string_view trimmed(std::u32string_view text)
{
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}
} // namespace detail

Transliteration Sanskrit::transliterate(std::string_view text, Script from, Script to)
{
    std::string error;
    const auto input = detail::normalizedText(text, error);
    if (!error.empty())
        return {{}, std::move(error)};
    if (from == to)
        return {detail::encode(input), {}};
    return {render(tokenize(input, from, false), to), {}};
}

Transliteration Sanskrit::transliterateWord(std::string_view word, Script from, Script to)
{
    std::string error;
    const auto normalized = detail::normalizedText(word, error);
    if (!error.empty())
        return {{}, std::move(error)};
    const auto input = detail::trimmed(normalized);
    if (input.empty() || input.size() > 128) {
        return {
            {},
            from == Script::Devanagari ? "Enter one Devanagari word (at most 128 characters)"
                                       : "Enter one Sanskrit word (at most 128 characters)"};
    }
    const auto tokens = tokenize(input, from, true);
    if (!isWord(tokens, from)) {
        return {
            {},
            from == Script::Devanagari ? "Enter one Devanagari word; spaces, accents and "
                                         "non-Sanskrit letters are unsupported"
                                       : "Enter one Sanskrit word in the specified script"};
    }
    return {render(tokens, to), {}};
}
} // namespace sanskrit
