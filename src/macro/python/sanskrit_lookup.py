"""Single Devanagari word: Cologne dictionary plus MW inflected forms."""

from macro_context import get_context
from sanskrit_macros.dictionary import lookup_word


def macro(txt):
    return lookup_word(txt, get_context()["dictionary"])
