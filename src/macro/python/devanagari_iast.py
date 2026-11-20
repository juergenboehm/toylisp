"""Insert an IAST line after each line containing only Devanagari text."""

import unicodedata

from indic_transliteration import detect, sanscript


def is_devanagari_line(text):
    has_letter = False
    for char in text:
        category = unicodedata.category(char)
        if "DEVANAGARI" in unicodedata.name(char, ""):
            has_letter = has_letter or category.startswith("L")
        elif char in " \t" or category.startswith("P"):
            continue
        else:
            return False
    return has_letter and detect.detect(text) == sanscript.DEVANAGARI


def macro(txt):
    result = []
    for line in txt.splitlines(keepends=True):
        if line.endswith("\r\n"):
            text, ending = line[:-2], "\r\n"
        elif line.endswith(("\n", "\r", "\u2028", "\u2029")):
            text, ending = line[:-1], line[-1:]
        else:
            text, ending = line, ""

        result.append(line)
        if is_devanagari_line(text):
            if not ending:
                result.append("\n")
            result.append(sanscript.transliterate(text, sanscript.DEVANAGARI, sanscript.IAST))
            result.append(ending)
    return "".join(result)
