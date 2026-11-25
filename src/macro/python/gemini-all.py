from macro_runtime import client

def macro(text):
    prompt = f"Übersetze diesen Sanskrit-Satz wörtlich und sinngemäß ins Deutsche:\n{text}"

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


    response = client.models.generate_content(
        model="gemini-3.5-flash",
        contents=prompt,
        )

    return (text + "\n" + response.text)
