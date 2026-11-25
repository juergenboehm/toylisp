"""NFC transliteration for the Sanskrit alphabet supported by ToyLisp.

Keep strict word validation separate from whole-text conversion. In particular,
do not repair a missing virama or change vowel length before dictionary lookup.
"""

import unicodedata


VOWELS = list(zip(
    "aAiIuUfFxXeEoO",
    ("a", "ā", "i", "ī", "u", "ū", "ṛ", "ṝ", "ḷ", "ḹ", "e", "ai", "o", "au"),
    "अआइईउऊऋॠऌॡएऐओऔ",
    ("", "ा", "ि", "ी", "ु", "ू", "ृ", "ॄ", "ॢ", "ॣ", "े", "ै", "ो", "ौ"),
))
CONSONANTS = list(zip(
    "kKgGNcCjJYwWqQRtTdDnpPbBmyrlvS zsh".replace(" ", ""),
    ("k", "kh", "g", "gh", "ṅ", "c", "ch", "j", "jh", "ñ", "ṭ", "ṭh", "ḍ", "ḍh",
     "ṇ", "t", "th", "d", "dh", "n", "p", "ph", "b", "bh", "m", "y", "r", "l",
     "v", "ś", "ṣ", "s", "h"),
    "कखगघङचछजझञटठडढणतथदधनपफबभमयरलवशषसह",
))
MARKS = list(zip("MH~'", ("ṃ", "ḥ", "~", "'"), "ंःँऽ"))
LETTERS = {row[0]: row for row in VOWELS + CONSONANTS + MARKS}
DEVANAGARI = {row[2]: row[0] for row in LETTERS.values()}
IAST = {row[1]: row[0] for row in LETTERS.values()}
SIGNS = {row[3]: row[0] for row in VOWELS if row[3]}
VOWEL_KEYS = {row[0] for row in VOWELS}
CONSONANT_KEYS = {row[0] for row in CONSONANTS}
MARK_KEYS = {row[0] for row in MARKS}


def is_slp1_word(text):
    return (isinstance(text, str) and 0 < len(text) <= 128
            and all(letter in LETTERS for letter in text)
            and any(letter not in MARK_KEYS for letter in text))


def to_slp1(text, script="devanagari", strict=False):
    text = unicodedata.normalize("NFC", text)
    tokens = []
    index = 0
    while index < len(text):
        char = text[index]
        if script == "devanagari":
            letter = DEVANAGARI.get(char)
        elif script == "iast":
            letter = IAST.get(text[index:index + 2].lower())
            if letter:
                index += 1
            else:
                letter = IAST.get(char.lower())
        else:
            letter = char if char in LETTERS else None

        if not strict and script != "devanagari":
            if char == "'":
                following = text[index + 1:index + 2]
                after = (IAST.get(following.lower()) if script == "iast"
                         else following if following in LETTERS else None)
                if not tokens or tokens[-1] not in LETTERS or not after or after in MARK_KEYS:
                    letter = None
            if char == "~" and (not tokens or tokens[-1] not in LETTERS):
                letter = None

        if strict and (letter is None or (not tokens and letter in MARK_KEYS)):
            raise ValueError("Enter one Devanagari word; spaces, accents and "
                             "non-Sanskrit letters are unsupported")
        tokens.append(letter if letter is not None else char)
        if script == "devanagari" and letter in CONSONANT_KEYS:
            following = text[index + 1:index + 2]
            if following == "्":
                index += 1
                if text[index + 1:index + 2] in ("\u200c", "\u200d"):
                    index += 1
            elif following in SIGNS:
                tokens.append(SIGNS[following])
                index += 1
            else:
                tokens.append("a")
        index += 1
    return "".join(tokens)


def word_to_slp1(text):
    text = unicodedata.normalize("NFC", text).strip()
    if not text or len(text) > 128:
        raise ValueError("Enter one Devanagari word (at most 128 characters)")
    result = to_slp1(text, strict=True)
    if not is_slp1_word(result):
        raise ValueError("Enter one Devanagari word")
    return result


def devanagari(text):
    output = []
    pending = False
    for char in text:
        letter = LETTERS.get(char)
        if pending and char in VOWEL_KEYS:
            output.append(letter[3])
            pending = False
            continue
        if pending:
            output.append("्")
        output.append(letter[2] if letter else char)
        pending = char in CONSONANT_KEYS
    if pending:
        output.append("्")
    return "".join(output)


def iast(text):
    return "".join(LETTERS[char][1] if char in LETTERS else char for char in text)
