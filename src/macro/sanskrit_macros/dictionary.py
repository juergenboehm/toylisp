"""Cologne morphology and conservative TEI extraction, implemented in Python."""

from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import json
import re
from threading import Lock
from urllib.parse import parse_qs, urlencode, urlparse
import xml.etree.ElementTree as ET

from .network import ProviderError, Transport
from .transliteration import devanagari, is_slp1_word, word_to_slp1


DICTIONARIES = "https://api.c-salt.uni-koeln.de/dicts/"
INFLECTED_FORMS = "https://www.sanskrit-lexicon.uni-koeln.de/scans/csl-inflect/web/getWord.php"
TEI = "{http://www.tei-c.org/ns/1.0}"
XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"


def unique(values):
    return list(dict.fromkeys(values))


def compact(text):
    return " ".join(text.split())


def name(node):
    return "" if isinstance(node, str) else node.tag.rsplit("}", 1)[-1]


def children(node):
    if node.text:
        yield node.text
    for child in node:
        yield child
        if child.tail:
            yield child.tail


def literary(node):
    return name(node) == "cit" and node.get("type") == "literary_source"


def visible(node, sanskrit=False):
    if isinstance(node, str):
        return devanagari(re.sub(r"[/\\^]", "", node)) if sanskrit else node
    tag = name(node)
    if (tag in ("note", "gramGrp", "re") or literary(node)
            or (tag == "ref" and node.get("type") == "facs")):
        return ""
    if tag == "choice":
        units = list(children(node))
        selected = next((unit for unit in units if not isinstance(unit, str)
                         and unit.get("ana") == "as1"), units[0] if units else "")
        return visible(selected)
    if tag == "g":
        return "√" if "root" in node.get("ref", "") else ""
    sanskrit = sanskrit or node.get(XML_LANG, "").startswith("san-Latn-x-SLP1")
    return "".join(visible(child, sanskrit) for child in children(node))


def english_gloss(sense):
    units = list(children(sense))
    start = next((index + 1 for index, child in enumerate(units)
                  if name(child) == "milestone"
                  and child.get("unit") == "following_word_part_of_inf"), 0)
    text = ""
    brackets = 0
    skip = False
    for child in units[start:]:
        if name(child) in ("sense", "re"):
            break
        unit = child.get("unit", "") if not isinstance(child, str) else ""
        if name(child) == "pc" and unit.endswith(("-left", "-right")):
            if unit.endswith("-left"):
                if brackets == 0 and not text.strip():
                    skip = True
                brackets += 1
            else:
                brackets = max(0, brackets - 1)
                if skip and brackets == 0:
                    skip = False
                    continue
        if skip:
            continue
        if literary(child) and brackets == 0 and text.strip():
            break
        text += visible(child)
    return compact(text)


def sense_label(node):
    return (name(node) == "sense" and bool(node.get("n"))
            and compact(visible(node)) == node.get("n"))


def uses_previous(sense):
    preamble = ""
    for child in children(sense):
        if isinstance(child, str):
            preamble += child
        elif name(child) != "gramGrp":
            break
    return bool(re.match(r"^dass\.(?:\s|:|$)", compact(preamble)))


def german_gloss(sense):
    text = ""
    collecting = False
    inline = False
    for child in children(sense):
        if not collecting and sense_label(child):
            inline = True
            continue
        if name(child) == "sense":
            break
        if name(child) == "hi" and child.get("rendition") == "#i":
            text += visible(child)
            collecting = True
        elif collecting:
            if not isinstance(child, str):
                break
            text += child
    return compact(text), inline


def collect_glosses(sense, dictionary, glosses, references, allow_previous=False):
    if (allow_previous and not sense.get("n") and dictionary == "pwg"
            and uses_previous(sense)):
        references.append(True)
        return
    gloss, inline = german_gloss(sense) if dictionary == "pwg" else (english_gloss(sense), False)
    if gloss and gloss not in glosses:
        glosses.append(gloss)
    if dictionary == "pwg" and gloss:
        if inline:
            for child in sense:
                if name(child) == "sense" and child.get("ana") == "e" and not sense_label(child):
                    collect_glosses(child, dictionary, glosses, references)
        return
    previous_number = 0
    for child in sense:
        if name(child) != "sense":
            continue
        number = re.match(r"^(\d+)", child.get("n", ""))
        if number:
            current = int(number[1])
            if previous_number and current <= previous_number:
                break
            previous_number = current
        collect_glosses(child, dictionary, glosses, references)


def entry_headword(entry):
    for form in entry:
        if name(form) == "form":
            for orth in form:
                if name(orth) == "orth" and orth.get("ana") == "key1":
                    return "".join(orth.itertext()).strip()
    return ""


def record_number(entry):
    numbers = set()
    stack = [entry]
    while stack:
        node = stack.pop()
        if name(node) == "re":
            continue
        if name(node) == "note" and node.get("ana") == "tail":
            for child in node:
                value = "".join(child.itertext()).strip()
                if name(child) == "idno" and child.get("ana") == "L" and value.isdecimal():
                    if 0 < int(value) <= 2**63 - 1:
                        numbers.add(int(value))
        stack.extend(node)
    return next(iter(numbers)) if len(numbers) == 1 else 0


def parse_xml(text):
    if re.search(r"<!\s*(?:DOCTYPE|ENTITY)\b", text, re.I):
        raise ProviderError("Unsupported XML entity or document type")
    try:
        root = ET.fromstring(text)
    except ET.ParseError as error:
        raise ProviderError("Invalid dictionary XML: " + str(error)) from error
    if root.tag != TEI + "entry":
        raise ProviderError("Expected a TEI dictionary entry")
    stack = [(root, 0)]
    remaining = 100000
    while stack:
        node, depth = stack.pop()
        remaining -= 1 + bool(node.text) + bool(node.tail)
        if depth > 64 or remaining < 0:
            raise ProviderError("XML structure exceeds the size limit")
        stack.extend((child, depth + 1) for child in node)
    return root


@dataclass
class Parsed:
    glosses: list = field(default_factory=list)
    references: list = field(default_factory=list)
    source: str = ""
    errors: list = field(default_factory=list)


def parse_dictionary(body, headword, dictionary, selection="headword", expected_record=0):
    try:
        document = json.loads(body)
        entries = document["data"]["entries"]
    except (ValueError, KeyError, TypeError) as error:
        raise ProviderError("Missing or invalid dictionary entries array") from error
    if not isinstance(entries, list):
        raise ProviderError("Missing dictionary entries array")
    if len(entries) >= 20:
        raise ProviderError("Dictionary result limit reached; results may be incomplete")
    result = Parsed()
    matching_records = 0
    for entry in entries:
        returned = entry.get("headword_slp1") if isinstance(entry, dict) else None
        if (not is_slp1_word(returned) or not isinstance(entry.get("xml"), str)
                or (selection == "headword" and returned != headword)):
            result.errors.append("Unexpected dictionary entry format")
            continue
        try:
            root = parse_xml(entry["xml"])
        except ProviderError as error:
            result.errors.append(str(error))
            continue
        selected = [root]
        if selection != "headword":
            if entry_headword(root) != returned:
                result.errors.append("Dictionary headword does not match its XML")
                continue
            if selection == "subentry":
                selected = [node for node in root.iter() if name(node) == "re"
                            and entry_headword(node) == headword]
                if not selected:
                    result.errors.append("No matching dictionary subentry")
            elif record_number(root) == expected_record:
                matching_records += 1
                result.source = returned
            else:
                continue
        glosses = []
        references = []
        for selected_entry in selected:
            for child in selected_entry:
                if name(child) == "sense":
                    collect_glosses(child, dictionary, glosses, references, True)
        if not selected:
            continue
        if references:
            number = record_number(root)
            if number <= 1:
                result.errors.append("Cannot resolve PWG 'dass.' without a preceding record number")
            else:
                result.references.append(number - 1)
        if not glosses and not references:
            result.errors.append("No supported meaning structure in dictionary entry")
        result.glosses.extend(glosses)
    if selection == "record":
        if not matching_records:
            result.errors.append("Referenced PWG record not found")
        elif matching_records > 1:
            result.errors.append("Ambiguous PWG record number")
            result.glosses.clear()
            result.references.clear()
            result.source = ""
    result.glosses = unique(result.glosses)
    result.references = unique(result.references)
    result.errors = unique(result.errors)
    return result


def parse_inflected_forms(body):
    text = body.decode("utf-8")
    if "<disp1>" not in text:
        raise ProviderError("Unexpected inflected-forms response")
    summary = text.rsplit("<disp1>", 1)[1]
    words = []
    for url in re.findall(r"id_info\('([^']+)'\)", summary):
        word = parse_qs(urlparse(url.replace("&amp;", "&")).query).get("key", [""])[0]
        if not is_slp1_word(word):
            raise ProviderError("Invalid headword in inflected-forms response")
        words.append(word)
    words = unique(words)
    if not words and summary.strip():
        raise ProviderError("Unexpected inflected-forms summary format")
    if len(words) > 16:
        raise ProviderError("More than 16 possible base forms; narrow the input")
    return words


class DictionaryClient:
    def __init__(self, dictionary, transport=None, dictionaries=DICTIONARIES,
                 inflected_forms=INFLECTED_FORMS):
        if dictionary not in ("pwg", "mw"):
            raise ValueError("Dictionary must be pwg or mw")
        self.dictionary = dictionary
        self.transport = transport or Transport()
        self.dictionaries = dictionaries
        self.inflected_forms = inflected_forms
        self.references = set()
        self.reference_lock = Lock()

    def fetch(self, field, value, mode="term"):
        query = urlencode({"field": field, "query": value, "query_type": mode, "size": 20})
        return self.transport.request(
            self.dictionaries + self.dictionary + "/restful/entries?" + query)

    def diagnostic(self, word, message, reference=False):
        label = "PWG reference" if reference else self.dictionary.upper()
        return f"{devanagari(word)} ({label}): {message}"

    def previous(self, word, record, depth, diagnostics):
        with self.reference_lock:
            key = (word, record)
            if key in self.references:
                return []
            if depth >= 4 or len(self.references) >= 16:
                diagnostics.append(self.diagnostic(word, "Dictionary reference limit reached", True))
                return []
            self.references.add(key)
        try:
            parsed = parse_dictionary(self.fetch("xml", str(record), "match"), "", "pwg",
                                      "record", record)
        except (ProviderError, UnicodeError) as error:
            diagnostics.append(self.diagnostic(word, str(error), True))
            return []
        diagnostics.extend(self.diagnostic(word, message, True) for message in parsed.errors)
        meanings = [f"wie {devanagari(parsed.source)}: {gloss}" for gloss in parsed.glosses]
        for previous in parsed.references:
            meanings.extend(self.previous(word, previous, depth + 1, diagnostics))
        return meanings

    def lookup(self, word):
        diagnostics = []
        try:
            parsed = parse_dictionary(self.fetch("headword_slp1", word), word, self.dictionary)
            if self.dictionary == "mw" and not parsed.errors and not parsed.glosses:
                parsed = parse_dictionary(self.fetch("re_headwords_slp1", word), word,
                                          self.dictionary, "subentry")
        except (ProviderError, UnicodeError) as error:
            return word, [], [self.diagnostic(word, str(error))]
        diagnostics.extend(self.diagnostic(word, message) for message in parsed.errors)
        inherited = []
        for record in sorted(parsed.references):
            inherited.extend(self.previous(word, record, 0, diagnostics))
        return word, inherited + parsed.glosses, diagnostics

    def lookup_many(self, words):
        words = unique(words)
        if len(words) > 64 or any(not is_slp1_word(word) for word in words):
            raise ValueError("Enter at most 64 valid SLP1 headwords")
        with ThreadPoolExecutor(max_workers=8) as pool:
            return list(pool.map(self.lookup, words))

    def base_forms(self, word):
        query = urlencode({"word": word, "transLit": "SLP2SLP", "filter": "SLP2SLP"})
        return parse_inflected_forms(self.transport.request(self.inflected_forms + "?" + query))


def lookup_word(text, dictionary="pwg", client=None):
    word = word_to_slp1(text)
    client = client or DictionaryClient(dictionary)
    diagnostics = []
    base_forms = []
    with ThreadPoolExecutor(max_workers=2) as pool:
        direct = pool.submit(client.lookup, word)
        morphology = pool.submit(client.base_forms, word)
        entries = [direct.result()]
        try:
            base_forms = morphology.result()
        except (ProviderError, UnicodeError) as error:
            diagnostics.append("Inflected forms: " + str(error))
    entries.extend(client.lookup_many([base for base in base_forms if base != word]))
    lines = []
    for headword, meanings, errors in entries:
        lines.extend(f"{devanagari(headword)} — {meaning}" for meaning in meanings)
        diagnostics.extend(errors)
    has_meanings = bool(lines)
    if not has_meanings and base_forms:
        lines.append("Recognized base form(s): " + ", ".join(map(devanagari, base_forms)))
    if diagnostics:
        lines.append("Lookup incomplete:")
        lines.extend(diagnostics)
    elif not has_meanings:
        lines.append("No dictionary meanings found in " + dictionary.upper() if base_forms
                     else "No dictionary entry or supported base form found for " + devanagari(word))
    return "\n".join(lines)
