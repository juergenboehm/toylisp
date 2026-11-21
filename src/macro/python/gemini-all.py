from macro_runtime import client

def macro(text):
    prompt = f"Übersetze diesen Sanskrit-Satz wörtlich und sinngemäß ins Deutsche:\n{text}"

    response = client.models.generate_content(
        model="gemini-3.5-flash",
        contents=prompt,
        )

    return (text + "\n" + response.text)
