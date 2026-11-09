#pragma once

#include <string>
#include <string_view>

namespace sanskrit::detail {
bool isSlp1Word(std::string_view word);

std::string encode(std::u32string_view text);

std::u32string normalizedText(std::string_view text, std::string &error);

std::u32string_view trimmed(std::u32string_view text);
} // namespace sanskrit::detail
