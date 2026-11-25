"""Bounded provider requests using Python's verified default TLS settings."""

import json
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen


class ProviderError(ValueError):
    pass


class Transport:
    def request(self, url, payload=None, timeout=20):
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        headers = {"User-Agent": "ToyLisp-Python-Macros/0.1",
                   "Accept": "application/json, text/html;q=0.9"}
        if data is not None:
            headers["Content-Type"] = "application/json"
        request = Request(url, data=data, headers=headers)
        deadline = time.monotonic() + timeout
        try:
            with urlopen(request, timeout=timeout) as response:
                body = bytearray()
                while True:
                    if time.monotonic() >= deadline:
                        raise ProviderError("Request timed out")
                    chunk = response.read1(64 * 1024)
                    if not chunk:
                        break
                    body.extend(chunk)
                    if len(body) > 4 * 1024 * 1024:
                        raise ProviderError("Provider response exceeds the size limit")
                return bytes(body)
        except HTTPError as error:
            error.close()
            raise ProviderError(str(error)) from error
        except (OSError, ValueError) as error:
            raise ProviderError(str(error)) from error
