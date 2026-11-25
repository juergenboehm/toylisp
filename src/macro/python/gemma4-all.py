from macro_runtime import client_o

import contextlib
import json
import sys
import traceback


with contextlib.redirect_stdout(sys.stderr):
    import requests
    from indic_transliteration import sanscript


def analyze_sanskrit(text, scheme):
    if not isinstance(text, str) or not text.strip() or len(text) > 4000:
        raise ValueError("Enter Sanskrit text of at most 4000 characters")
    schemes = {"devanagari": sanscript.DEVANAGARI, "iast": sanscript.IAST, "slp1": sanscript.SLP1}

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

    ret_txt = ""

    txt_unsandhied_deva = ""
    txt_unsandhied = ""
    txt_tags = "| "

    for word in words:
        form = word["form"]
        txt_unsandhied += form + " | "
        txt_unsandhied_deva += sanscript.transliterate(form, sanscript.IAST, sanscript.DEVANAGARI) + " "
        tag = word["tag"]
        txt_tags += tag + " | "

    txt_all = txt_unsandhied_deva + "\n"
    txt_all = txt_all + txt_unsandhied + "\n"
    txt_all = txt_all + txt_tags + "\n"

    return txt_all



def macro(text):

    text1 = text.strip()

    txt_res = analyze_sanskrit(text1, sanscript.DEVANAGARI)
    return text + "\n" + txt_res

    prompt = f"""
    Analysiere den folgenden Sanskrit-Text.

    1. Zerlege Sandhi-Verbindungen, soweit möglich.
    2. Gib die einzelnen Wörter in Devanagari und IAST an.
    3. Bestimme ihre grammatische Form.
    4. Gib eine möglichst wörtliche deutsche Übersetzung.
    5. Gib eine sinngemäße, flüssige deutsche Übersetzung.
    6. Erläutere interessante grammatische Besonderheiten.

    Wenn eine Analyse unsicher ist, kennzeichne dies.
    Erfinde keine Wortformen.

    Sanskrit-Text:
    {text}
    """

    response = client_o.chat(
        model="gemma4:12b",
        messages=[
            {"role": "user", "content": prompt}
        ]
    )

    return text + "\n" + response.message.content