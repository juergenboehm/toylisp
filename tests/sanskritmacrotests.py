"""Offline regressions for the independent Python Sanskrit implementation."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
from threading import Thread
import time
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src/macro"))

from sanskrit_macros.dictionary import (DictionaryClient, lookup_word, parse_dictionary,
                                        parse_inflected_forms)
from sanskrit_macros.network import ProviderError, Transport
from sanskrit_macros.transliteration import devanagari, iast, to_slp1, word_to_slp1
from sanskrit_macros.verse import analyze_verse, compact_grammar, parse_analysis

FIXTURES = ROOT / "tests/fixtures/sanskrit"
EMPTY = b'{"data":{"entries":[]}}'


def fixture(filename):
    return (FIXTURES / filename).read_bytes()


def dictionary(headword, meaning, german=False, record=0):
    gloss = '<hi rendition="#i">' + meaning + '</hi>' if german else meaning
    xml = ('<entry xmlns="http://www.tei-c.org/ns/1.0"><form><orth ana="key1">'
           + headword + '</orth></form><sense>' + gloss + '</sense>')
    if record:
        xml += '<note ana="tail"><idno ana="L">' + str(record) + '</idno></note>'
    return json.dumps({"data": {"entries": [{"headword_slp1": headword,
                                            "xml": xml + "</entry>"}]}}).encode()


def recorded_analysis():
    words = json.loads(fixture("dharmamitra-gita-2-47.json"))["words"]
    return json.dumps([{"grammatical_analysis": [
        {"unsandhied": word["form"], "lemma": word["lemma"], "tag": word["tag"]}
        for word in words]}]).encode()


class RecordedTransport:
    def __init__(self, handler):
        self.handler = handler
        self.requests = []

    def request(self, url, payload=None, timeout=20):
        self.requests.append((url, payload))
        return self.handler(url, payload)


class SanskritMacroTests(unittest.TestCase):
    def test_transliteration_preserves_spelling(self):
        for text, slp in [("रामेण", "rAmeRa"), ("गच्छति", "gacCati"), ("भवति", "Bavati"),
                          ("दूरतस्", "dUratas"), ("दूरतस", "dUratasa"), ("भु", "Bu"),
                          ("भू", "BU"), ("वसुधैव", "vasuDEva"), ("कुटुम्बकम्", "kuwumbakam"),
                          ("अँऽ", "a~'")]:
            with self.subTest(text=text):
                self.assertEqual(word_to_slp1(text), slp)
                self.assertEqual(devanagari(slp), text)
                self.assertEqual(to_slp1(iast(slp), "iast", strict=True), slp)
        self.assertEqual(word_to_slp1("  ग्‍च्छति\n"), "gcCati")
        self.assertEqual(to_slp1("kṛṣṇa", "iast"), "kfzRa")

    def test_invalid_word_never_contacts_provider(self):
        transport = RecordedTransport(lambda *args: self.fail("Unexpected provider request"))
        client = DictionaryClient("pwg", transport)
        for text in ("", "agni", "राम कृष्ण", "राम\nकृष्ण", "ि", "काि", "क़", "ॐ", "अ" * 129):
            with self.subTest(text=text), self.assertRaises(ValueError):
                lookup_word(text, client=client)

    def test_inflected_forms_and_limits(self):
        for filename, expected in [("ramena.html", ["rAma"]), ("gacchati.html", ["gam"]),
                                   ("bhavati.html", ["BavatI", "Bavat", "BU"]),
                                   ("kutumbakam.html", ["kuwumbaka"])]:
            self.assertEqual(parse_inflected_forms(fixture(filename)), expected)
        self.assertEqual(parse_inflected_forms(b"<disp1>\n"), [])
        for body in (b"<html>error</html>", b"<disp1>unexpected",
                     b"<disp1>id_info('https://x/?key=invalid123')",
                     ("<disp1>" + "".join("id_info('https://x/?key=" + "a" * i + "')"
                                            for i in range(1, 18))).encode()):
            with self.assertRaises(ProviderError):
                parse_inflected_forms(body)

    def test_recorded_tei_glosses(self):
        agni = parse_dictionary(fixture("mw-agni.json"), "agni", "mw")
        self.assertFalse(agni.errors)
        self.assertEqual(len(agni.glosses), 9)
        self.assertTrue(agni.glosses[0].startswith("fire, sacrificial fire"))
        self.assertIn("gold", agni.glosses)
        self.assertNotIn("fire-particle", " ".join(agni.glosses))
        gam = parse_dictionary(fixture("pwg-gam.json"), "gam", "pwg")
        self.assertFalse(gam.errors)
        self.assertEqual(len(gam.glosses), 10)
        self.assertTrue(gam.glosses[0].startswith("gehen, sich bewegen"))
        self.assertNotIn("Gang, Art zu gehen", " ".join(gam.glosses))
        bhu = parse_dictionary(fixture("mw-bhu.json"), "BU", "mw")
        self.assertTrue(bhu.glosses[0].startswith("to become, be"))
        self.assertNotIn("Dha1tup", " ".join(bhu.glosses))

    def test_german_subsenses_and_partial_entries(self):
        vasudha = parse_dictionary(fixture("pwg-vasudha.json"), "vasuDA", "pwg")
        self.assertFalse(vasudha.errors)
        self.assertEqual(len(vasudha.glosses), 3)
        self.assertIn("die Erde; Land, Reich", vasudha.glosses)
        self.assertNotIn("regiert habend", " ".join(vasudha.glosses))
        eva = parse_dictionary(fixture("pwg-eva.json"), "eva", "pwg")
        self.assertTrue(eva.errors)
        self.assertIn("gerade, eben, kaum, nur, noch, schon", " ".join(eva.glosses))
        for reverse in (False, True):
            entries = (json.loads(dictionary("eva", "nur", True))["data"]["entries"]
                       + json.loads(dictionary("eva", "unsupported"))["data"]["entries"])
            if reverse:
                entries.reverse()
            result = parse_dictionary(json.dumps({"data": {"entries": entries}}), "eva", "pwg")
            self.assertEqual(result.glosses, ["nur"])
            self.assertTrue(result.errors)

    def test_mw_subentries_and_pwg_predecessor(self):
        result = parse_dictionary(fixture("mw-kutumba.json"), "kuwumbaka", "mw", "subentry")
        self.assertFalse(result.errors)
        self.assertEqual(len(result.glosses), 3)
        self.assertEqual(result.glosses[0], "a household, family")
        self.assertNotIn("domestic dissension", " ".join(result.glosses))
        self.assertNotIn("an attentive father", " ".join(result.glosses))
        result = parse_dictionary(fixture("pwg-kutumbaka.json"), "kuwumbaka", "pwg")
        self.assertEqual(result.references, [17548])
        self.assertFalse(result.errors)
        result = parse_dictionary(fixture("pwg-before-kutumbaka.json"), "", "pwg", "record", 17548)
        self.assertEqual(result.source, "kuwumba")
        self.assertTrue(result.glosses[0].startswith("Hausstand"))
        for body in (fixture("pwg-before-kutumbaka.json"), dictionary("rAma", "citation 17547", True, 900)):
            result = parse_dictionary(body, "", "pwg", "record", 17547)
            self.assertTrue(result.errors)
            self.assertEqual(result.glosses, [])

    def test_invalid_dictionary_json_xml_and_limits(self):
        for body in (b"invalid", b"[]", b"{}", b'{"data":{"entries":{}}}'):
            with self.assertRaises(ProviderError):
                parse_dictionary(body, "agni", "mw")
        for body in (dictionary("other", "meaning"), dictionary("agni", "<unclosed>"),
                     dictionary("agni", "<sense>" * 66 + "text" + "</sense>" * 66)):
            self.assertTrue(parse_dictionary(body, "agni", "mw").errors)
        entry = json.loads(dictionary("agni", "fire"))["data"]["entries"][0]
        with self.assertRaises(ProviderError):
            parse_dictionary(json.dumps({"data": {"entries": [entry] * 20}}), "agni", "mw")
        entry["xml"] = '<!DOCTYPE entry [<!ENTITY x "boom">]>' + entry["xml"]
        self.assertTrue(parse_dictionary(json.dumps({"data": {"entries": [entry]}}), "agni", "mw").errors)

    def kutumbakam_transport(self, language):
        def handler(url, payload):
            query = parse_qs(urlparse(url).query)
            if "entries" in url:
                self.assertIn("/" + language + "/", url)
            if "word" in query:
                return fixture("kutumbakam.html")
            key, field = query["query"][0], query["field"][0]
            if key == "kuwumbaka" and language == "pwg":
                return fixture("pwg-kutumbaka.json")
            if key == "17548":
                self.assertEqual(field, "xml")
                self.assertEqual(query["query_type"], ["match"])
                return fixture("pwg-before-kutumbaka.json")
            if key == "kuwumbaka" and field == "re_headwords_slp1":
                return fixture("mw-kutumba.json")
            return EMPTY
        return RecordedTransport(handler)

    def test_lookup_both_languages_with_morphology_and_references(self):
        for language, expected in (("pwg", "कुटुम्बक — wie कुटुम्ब: Hausstand"),
                                   ("mw", "कुटुम्बक — a household, family")):
            transport = self.kutumbakam_transport(language)
            result = lookup_word("कुटुम्बकम्", language, DictionaryClient(language, transport))
            self.assertIn(expected, result)
            self.assertNotIn("Lookup incomplete", result)
            queries = [parse_qs(urlparse(url).query) for url, _ in transport.requests]
            self.assertEqual(sum(query.get("query") == ["kuwumbaka"]
                                 and query.get("field") == ["headword_slp1"] for query in queries), 1)

    def test_no_matches_partial_failure_and_recognized_form(self):
        def handler(url, payload):
            return b"<disp1>" if "getWord" in url else EMPTY
        result = lookup_word("भु", client=DictionaryClient("pwg", RecordedTransport(handler)))
        self.assertIn("No dictionary entry or supported base form found for भु", result)
        result = lookup_word("कुटुम्बकम्", client=DictionaryClient("pwg", RecordedTransport(
            lambda url, _: fixture("kutumbakam.html") if "getWord" in url else EMPTY)))
        self.assertIn("Recognized base form(s): कुटुम्बक", result)
        self.assertIn("No dictionary meanings found in PWG", result)
        def partial(url, payload):
            if "getWord" in url:
                raise ProviderError("offline morphology")
            return fixture("pwg-eva.json")
        result = lookup_word("एव", client=DictionaryClient("pwg", RecordedTransport(partial)))
        self.assertIn("एव — gerade", result)
        self.assertIn("Inflected forms: offline morphology", result)
        self.assertIn("Lookup incomplete:", result)

    def test_reference_chain_budget_and_partial_meanings(self):
        def handler(url, payload):
            query = parse_qs(urlparse(url).query)
            if query["field"] == ["xml"]:
                return dictionary("rAma", "dass.", record=int(query["query"][0]))
            entries = json.loads(dictionary("rAma", "dass.", record=900))["data"]["entries"]
            entries += json.loads(dictionary("rAma", "retained", True))["data"]["entries"]
            return json.dumps({"data": {"entries": entries}}).encode()
        transport = RecordedTransport(handler)
        client = DictionaryClient("pwg", transport)
        _, meanings, errors = client.lookup("rAma")
        self.assertIn("retained", meanings)
        self.assertTrue(any("reference limit" in error for error in errors))
        self.assertEqual(len(client.references), 4)
        self.assertEqual(len(transport.requests), 5)
        # All headwords in one job share the 16-reference request budget.
        for word in ("a", "i", "u", "e", "o"):
            client.previous(word, 899, 0, [])
        self.assertEqual(len(client.references), 16)
        def failing_reference(url, payload):
            if parse_qs(urlparse(url).query)["field"] == ["xml"]:
                raise ProviderError("reference unavailable")
            return handler(url, payload)
        client = DictionaryClient("pwg", RecordedTransport(failing_reference))
        _, meanings, errors = client.lookup("rAma")
        self.assertIn("retained", meanings)
        self.assertTrue(any("reference unavailable" in error for error in errors))

    def test_grammar_uses_all_tags(self):
        self.assertEqual(compact_grammar("Case=Locative, Gender=Neuter, Number=Plural"), "Loc.Pl.n")
        self.assertEqual(compact_grammar("Mood=Optative, Person=Third, Number=Singular, Verb"),
                         "Sg.3p, Mood=Optative, Verb")
        self.assertEqual(compact_grammar(""), "Unspecified")

    def test_verse_recording_language_and_partial_dictionary_failure(self):
        for language in ("pwg", "mw"):
            def handler(url, payload):
                if payload:
                    self.assertEqual(payload["texts"], ["karmaṇyevādhikāraste mā phaleṣu kadācana"])
                    self.assertTrue(payload["human_readable_tags"])
                    return recorded_analysis()
                key = parse_qs(urlparse(url).query)["query"][0]
                self.assertIn("/" + language + "/", url)
                if key == "eva":
                    raise ProviderError("recorded dictionary failure")
                return dictionary(key, "Bedeutung" if language == "pwg" else "meaning", language == "pwg")
            transport = RecordedTransport(handler)
            result = analyze_verse("कर्मण्येवाधिकारस्ते मा फलेषु कदाचन", language, transport=transport)
            self.assertIn("Padapāṭha: कर्मणि | एव | अधिकारः | ते | मा | फलेषु | कदाचन", result)
            self.assertIn("karmaṇi (Loc.Sg.n)", result)
            self.assertIn("eva (Unspecified)", result)
            self.assertIn(language.upper() + "-Einträge:", result)
            self.assertIn("Bedeutung" if language == "pwg" else "meaning", result)
            self.assertIn("Lookup incomplete:", result)
            self.assertIn("recorded dictionary failure", result)

    def test_invalid_and_excessive_verse_analysis(self):
        for body in (b"{}", b"[]", b"invalid", b'[{"grammatical_analysis":{}}]',
                     b'[{"grammatical_analysis":[{"unsandhied":42,"lemma":"a","tag":""}]}]'):
            with self.assertRaises(ProviderError):
                parse_analysis(body)
        sentence = json.loads(recorded_analysis())[0]
        sentence["grammatical_analysis"] *= 10
        with self.assertRaises(ProviderError):
            parse_analysis(json.dumps([sentence]))
        for text in ("", "अ" * 4001):
            with self.assertRaises(ValueError):
                analyze_verse(text)

    def test_verse_distinct_and_unusable_lemmata(self):
        words = [{"unsandhied": "rāmaḥ", "lemma": "rāma", "tag": ""},
                 {"unsandhied": "rāmam", "lemma": "rāma", "tag": ""},
                 {"unsandhied": "eva", "lemma": "", "tag": ""}]
        def handler(url, payload):
            if payload:
                return json.dumps([{"grammatical_analysis": words}]).encode()
            self.assertEqual(parse_qs(urlparse(url).query)["query"], ["rAma"])
            return dictionary("rAma", "Rama", True)
        transport = RecordedTransport(handler)
        result = analyze_verse("रामः रामम् एव", transport=transport)
        self.assertIn("No usable dictionary headword for: एव", result)
        self.assertIn("राम (rāma → rāmaḥ): Rama", result)
        self.assertEqual(len(transport.requests), 2)

    def test_context_readonly_and_reset_on_exception(self):
        spec = importlib.util.spec_from_file_location(
            "macro_dispatcher", ROOT / "src/macro/dispatcher.py")
        dispatcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(dispatcher)
        with TemporaryDirectory() as directory:
            path = Path(directory) / "context.py"
            path.write_text("from macro_context import get_context\n"
                            "def macro(txt):\n    get_context()['dictionary'] = 'changed'\n")
            with self.assertRaises(TypeError):
                dispatcher.run_macro({"path": str(path), "text": "",
                                      "context": {"dictionary": "mw"}})
            path.write_text("from macro_context import get_context\n"
                            "def macro(txt): return get_context()['dictionary']\n")
            self.assertEqual(dispatcher.run_macro({"path": str(path), "text": ""}),
                             {"text": "pwg"})

    def test_selectable_macros_through_dispatcher(self):
        spec = importlib.util.spec_from_file_location("macro_dispatcher", ROOT / "src/macro/dispatcher.py")
        dispatcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(dispatcher)
        for language in ("pwg", "mw"):
            transport = self.kutumbakam_transport(language)
            with patch.object(Transport, "request", side_effect=transport.request):
                result = dispatcher.run_macro({
                    "path": str(ROOT / "src/macro/python/sanskrit_lookup.py"),
                    "text": "कुटुम्बकम्", "context": {"dictionary": language}})
            self.assertIn("Hausstand" if language == "pwg" else "a household, family", result["text"])
            def verse_response(url, payload=None, timeout=20):
                return recorded_analysis() if payload else EMPTY
            with patch.object(Transport, "request", side_effect=verse_response):
                result = dispatcher.run_macro({
                    "path": str(ROOT / "src/macro/python/sanskrit_verse.py"),
                    "text": "कर्मण्येवाधिकारस्ते", "context": {"dictionary": language}})
            self.assertIn("Padapāṭha:", result["text"])
            self.assertIn(language.upper() + "-Einträge:", result["text"])

    def test_real_http_transport_and_limits(self):
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                if self.path == "/slow":
                    time.sleep(0.1)
                body = b"x" * (4 * 1024 * 1024 + 1) if self.path == "/large" else b"recorded"
                self.send_response(503 if self.path == "/failure" else 200)
                self.end_headers()
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def do_POST(self):
                body = self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(body)

        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = Thread(target=server.serve_forever)
            thread.start()
            try:
                base = "http://127.0.0.1:" + str(server.server_port)
                transport = Transport()
                self.assertEqual(transport.request(base), b"recorded")
                self.assertEqual(json.loads(transport.request(base, {"text": "राम"})), {"text": "राम"})
                for path in ("/large", "/failure"):
                    with self.assertRaises(ProviderError):
                        transport.request(base + path)
                with self.assertRaises(ProviderError):
                    transport.request(base + "/slow", timeout=0.02)
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    unittest.main()
