"""Sanskrit analysis worker. Only protocol messages are written to standard output."""

import contextlib
import json
import sys
import traceback


with contextlib.redirect_stdout(sys.stderr):
    import requests
    from indic_transliteration import sanscript


def analyze_sanskrit(params):
    text = params["text"]
    if not isinstance(text, str) or not text.strip() or len(text) > 4000:
        raise ValueError("Enter Sanskrit text of at most 4000 characters")
    schemes = {"devanagari": sanscript.DEVANAGARI, "iast": sanscript.IAST, "slp1": sanscript.SLP1}
    scheme = schemes[params["scheme"]]
    iast = sanscript.transliterate(text, scheme, sanscript.IAST)
    with requests.post(
        "https://dharmamitra.org/api-tagging/tagging-parsed/",
        json={
            "texts": [iast],
            "mode": "unsandhied-lemma-morphosyntax",
            "human_readable_tags": True,
            "grammar_type": "western",
        },
        timeout=(5, 25),
        stream=True,
    ) as response:
        response.raise_for_status()
        body = bytearray()
        for chunk in response.iter_content(chunk_size=64 * 1024):
            body.extend(chunk)
            if len(body) > 4 * 1024 * 1024:
                raise ValueError("Dharmamitra response exceeds the size limit")
    return analyze_response(json.loads(body))


def analyze_response(sentences):
    if not isinstance(sentences, list) or not sentences:
        raise ValueError("Dharmamitra returned no sentence analysis")
    words = []
    notices = []
    for sentence in sentences:
        analysis = sentence["grammatical_analysis"]
        if not isinstance(analysis, list):
            raise ValueError("Dharmamitra returned an invalid grammatical analysis")
        for word in analysis:
            if len(words) >= 64:
                raise ValueError("Dharmamitra returned more than 64 words; shorten the input")
            form = word["unsandhied"]
            lemma = word["lemma"]
            tag = word["tag"]
            if not all(isinstance(value, str) for value in (form, lemma, tag)) or not form:
                raise ValueError("Dharmamitra returned an invalid word analysis")
            words.append({
                "form": form,
                "devanagari": sanscript.transliterate(form, sanscript.IAST, sanscript.DEVANAGARI),
                "lemma": lemma,
                "slp1": sanscript.transliterate(lemma, sanscript.IAST, sanscript.SLP1),
                "tag": tag,
            })
        if isinstance(sentence.get("notice"), str) and sentence["notice"].strip():
            notices.append(sentence["notice"])
    if not words:
        raise ValueError("Dharmamitra returned no words")
    return {"words": words, "notice": "\n".join(notices)}


METHODS = {"sanskrit.analyze": analyze_sanskrit}


def send(message):
    print(json.dumps(message, ensure_ascii=False, separators=(",", ":")), flush=True)


def main():
    send({"ready": 1})
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request["id"]
            with contextlib.redirect_stdout(sys.stderr):
                result = METHODS[request["method"]](request["params"])
        except Exception as error:
            traceback.print_exc(file=sys.stderr)
            send({"id": request_id, "error": f"{type(error).__name__}: {error}"})
        else:
            send({"id": request_id, "result": result})


if __name__ == "__main__":
    main()
