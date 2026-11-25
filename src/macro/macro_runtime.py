"""Shared initialization for ToyLisp macros.

Import this module explicitly from a macro, for example:
    from macro_runtime import client

The module is initialized once per macro interpreter, on its first import.
Use Actions → Restart Macro Interpreter after editing it. Macros that do not
import it, including devanagari_iast, do not need the Gemini SDK or credentials.
"""

# Add shared imports, constants, helper functions and initialization here.

# Gemini example: install google-genai in the configured Python environment.
# genai.Client() uses the credentials inherited from ToyLisp's environment,
# for example GEMINI_API_KEY or GOOGLE_API_KEY. Do not put keys in this file.
from google import genai
from ollama import Client

client = genai.Client()


client_o = Client(
    host="http://127.0.0.1:11434",
)