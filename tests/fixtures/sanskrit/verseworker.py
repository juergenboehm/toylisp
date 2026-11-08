"""Offline child process for testing the ToyLisp JSON channel."""

import json
import os
from pathlib import Path
import sys
import time


print(json.dumps({"ready": 1}), flush=True)
for line in sys.stdin:
    request = json.loads(line)
    method = request["method"]
    if method == "sanskrit.analyze":
        if request["params"].get("scheme") not in ("devanagari", "iast", "slp1"):
            print(json.dumps({"id": request["id"], "error": "Missing Sanskrit input scheme"}),
                  flush=True)
            continue
        fixture = Path(__file__).with_name("dharmamitra-gita-2-47.json")
        result = json.loads(fixture.read_text(encoding="utf-8"))
        result["notice"] = request["params"].get("notice", "")
    elif method == "delay":
        time.sleep(0.3)
        result = {}
    elif method == "crash":
        print("test worker failure", file=sys.stderr, flush=True)
        sys.exit(7)
    else:
        result = {"pid": os.getpid(), "params": request["params"]}
    print(json.dumps({"id": request["id"], "result": result}), flush=True)
