"""Dharmamitra verse analysis with Cologne glosses in the selected language."""

from macro_context import get_context
from sanskrit_macros.verse import analyze_verse


def macro(txt):
    return analyze_verse(txt, get_context()["dictionary"])
