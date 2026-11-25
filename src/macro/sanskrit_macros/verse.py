"""Dharmamitra analysis, compact grammar and Cologne lemma glosses."""

import json
import unicodedata

from .dictionary import DictionaryClient, compact, unique
from .network import ProviderError, Transport
from .transliteration import devanagari, iast, is_slp1_word, to_slp1


DHARMAMITRA = "https://dharmamitra.org/api-tagging/tagging-parsed/"
ABBREVIATIONS = {
    "Nominative": "Nom", "Vocative": "Voc", "Accusative": "Acc", "Instrumental": "Ins",
    "Dative": "Dat", "Ablative": "Abl", "Genitive": "Gen", "Locative": "Loc",
    "Singular": "Sg", "Dual": "Du", "Plural": "Pl", "Masculine": "m", "Feminine": "f",
    "Neuter": "n", "First": "1p", "Second": "2p", "Third": "3p",
}


def compact_grammar(tag):
    features = {}
    extra = []
    for feature in tag.split(","):
        feature = feature.strip()
        if not feature:
            continue
        if "=" not in feature:
            extra.append(feature)
            continue
        key, value = map(str.strip, feature.split("=", 1))
        if value in ABBREVIATIONS:
            features[key] = ABBREVIATIONS[value]
        else:
            extra.append(key + "=" + value)
    parts = [features.pop(key) for key in ("Case", "Number", "Gender", "Person") if key in features]
    parts.extend(features[key] for key in sorted(features))
    return ", ".join(part for part in (".".join(parts), ", ".join(extra)) if part) or "Unspecified"


def parse_analysis(body):
    try:
        sentences = json.loads(body)
        if not isinstance(sentences, list) or not sentences:
            raise ProviderError("Dharmamitra returned no sentence analysis")
        words = []
        notices = []
        for sentence in sentences:
            analysis = sentence["grammatical_analysis"]
            if not isinstance(analysis, list):
                raise ProviderError("Dharmamitra returned an invalid grammatical analysis")
            for word in analysis:
                if len(words) >= 64:
                    raise ProviderError("Dharmamitra returned more than 64 words; shorten the input")
                form, lemma, tag = (word[key] for key in ("unsandhied", "lemma", "tag"))
                if not all(isinstance(value, str) for value in (form, lemma, tag)) or not form.strip():
                    raise ProviderError("Dharmamitra returned an invalid word analysis")
                words.append({"form": compact(form), "lemma": compact(lemma),
                              "slp1": to_slp1(compact(lemma), "iast"), "tag": compact(tag),
                              "devanagari": devanagari(to_slp1(compact(form), "iast"))})
            notice = sentence.get("notice")
            if isinstance(notice, str) and notice.strip():
                notices.append(compact(notice))
        if not words:
            raise ProviderError("Dharmamitra returned no words")
        return words, " ".join(notices)
    except (ValueError, KeyError, TypeError) as error:
        raise ProviderError("Invalid Dharmamitra analysis: " + str(error)) from error


def analyze_verse(text, dictionary="pwg", client=None, transport=None, endpoint=DHARMAMITRA):
    text = unicodedata.normalize("NFC", text).strip()
    if not text or len(text.encode("utf-16-le")) // 2 > 4000:
        raise ValueError("Enter Sanskrit text of at most 4000 characters")
    transport = transport or Transport()
    payload = {"texts": [iast(to_slp1(text))], "mode": "unsandhied-lemma-morphosyntax",
               "human_readable_tags": True, "grammar_type": "western"}
    words, notice = parse_analysis(transport.request(endpoint, payload, timeout=25))
    client = client or DictionaryClient(dictionary, transport)
    lines = ["Padapāṭha: " + " | ".join(word["devanagari"] for word in words),
             "Grammatik: " + " | ".join(
                 word["form"] + " (" + compact_grammar(word["tag"]) + ")" for word in words)]
    if notice:
        lines.append("Dharmamitra: " + notice)
    lines.append(dictionary.upper() + "-Einträge:")
    unavailable = unique(word["devanagari"] for word in words if not is_slp1_word(word["slp1"]))
    if unavailable:
        lines.append("  No usable dictionary headword for: " + ", ".join(unavailable))
    labels = {}
    for word in words:
        form, lemma, headword = word["form"], word["lemma"], word["slp1"]
        label = lemma if form == lemma else lemma + " → " + form
        labels.setdefault(headword, devanagari(headword) + " (" + label + ")")
    diagnostics = []
    for headword, meanings, errors in client.lookup_many(
            [word["slp1"] for word in words if is_slp1_word(word["slp1"])]):
        gloss = "; ".join(meanings[:2])
        if len(meanings) > 2:
            gloss += " …"
        lines.append("  " + labels[headword] + ": "
                     + (gloss or "No usable " + dictionary.upper() + " gloss"))
        diagnostics.extend(errors)
    if diagnostics:
        lines.append("Lookup incomplete:")
        lines.extend(diagnostics)
    return "\n".join(lines)
