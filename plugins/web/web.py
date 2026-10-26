#!/usr/bin/env python3
"""Nexus web search plugin: `web <terms>` offers to open the terms in a search engine.

The engine comes from config.json next to this script: {"engine": "https://example.com/?q={query}"}.
"""
import json
import os
import sys
from urllib.parse import quote_plus

DEFAULT_ENGINE = "https://duckduckgo.com/?q={query}"


def engine():
    try:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")) as f:
            template = json.load(f)["engine"]
        return template if isinstance(template, str) and "{query}" in template else DEFAULT_ENGINE
    except (OSError, ValueError, KeyError, TypeError):
        return DEFAULT_ENGINE


def results(text):
    if not text.strip():
        return []
    url = engine().replace("{query}", quote_plus(text.strip()))
    return [{"id": "web", "title": "Search the web for " + text.strip(), "subtitle": url,
             "score": 1000, "action": "open", "payload": url}]


def send(message):
    print(json.dumps(message), flush=True)


send({"type": "hello", "api_version": 1})
for line in sys.stdin:
    try:
        message = json.loads(line)
    except ValueError:
        continue
    if not isinstance(message, dict):
        continue
    if message.get("type") == "search" and "id" in message:
        send({"type": "results", "id": message["id"], "results": results(message.get("text", ""))})
    elif message.get("type") == "shutdown":
        break
