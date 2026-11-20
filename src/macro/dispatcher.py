"""Run Python text macros; stdout is reserved for the JSON-line protocol."""

import contextlib
import json
from pathlib import Path
import sys
import traceback
import types


def run_macro(params):
    path = Path(params["path"])
    text = params["text"]
    if not isinstance(text, str):
        raise TypeError("Macro input must be a string")

    # Compile the file itself so rapid edits cannot reuse stale bytecode.
    module = types.ModuleType("_toylisp_macro")
    module.__file__ = str(path)
    sys.modules[module.__name__] = module
    sys.path.insert(0, str(path.parent))
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    function = getattr(module, "macro", None)
    if not callable(function):
        raise TypeError(f"{path.name} must define a callable macro(txt)")
    result = function(text)
    if not isinstance(result, str):
        raise TypeError("macro(txt) must return a string")
    return {"text": result}


def send(message):
    print(json.dumps(message, ensure_ascii=False, separators=(",", ":")), flush=True)


def main():
    send({"ready": 1})
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request["id"]
            if request["method"] != "macro.run":
                raise ValueError("Unknown macro request")
            with contextlib.redirect_stdout(sys.stderr):
                result = run_macro(request["params"])
        except BaseException as error:
            traceback.print_exc(file=sys.stderr)
            send({"id": request_id, "error": f"{type(error).__name__}: {error}"})
        else:
            send({"id": request_id, "result": result})


if __name__ == "__main__":
    main()
