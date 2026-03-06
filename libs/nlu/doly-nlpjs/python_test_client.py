#!/usr/bin/env python3
"""Basic Python client for Doly NLU (nlpjs) service.

Usage:
    python3 python_test_client.py [text]

If text is omitted, the script runs a few predefined checks:
  * GET /health
  * GET /stats
  * POST /parse with sample queries

Customize the SERVER_URL environment variable if the service listens on a
non-default host/port (default http://localhost:3000).
"""

import os
import sys
import json
import requests

SERVER_URL = os.environ.get("NLU_SERVER", "http://localhost:3000")


def get(path):
    url = SERVER_URL + path
    r = requests.get(url, timeout=5)
    print(f"GET {path} -> {r.status_code}")
    try:
        print(r.json())
    except Exception:
        print(r.text)
    print()


def parse(text):
    url = SERVER_URL + "/parse"
    payload = {"text": text}
    r = requests.post(url, json=payload, timeout=10)
    print(f"POST /parse '{text}' -> {r.status_code}")
    try:
        print(json.dumps(r.json(), ensure_ascii=False, indent=2))
    except Exception:
        print(r.text)
    print()


def main():
    if len(sys.argv) > 1:
        text = " ".join(sys.argv[1:])
        parse(text)
        return

    print("== health check ==")
    get("/health")

    print("== stats ==")
    get("/stats")

    samples = [
        "你好",
        "播放音乐",
        "打开灯",
        "今天天气如何",
    ]
    for s in samples:
        parse(s)


if __name__ == "__main__":
    main()
