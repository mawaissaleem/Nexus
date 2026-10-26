#!/usr/bin/env python3
"""Nexus system actions plugin: type `lock`, `sleep`, `logout`, `restart` or `shutdown`.

Claims the empty prefix, so it sees every unprefixed query. Results run an argv directly (no shell);
the destructive ones carry "confirm": true and Nexus asks yes/no before running them.
"""
import json
import os
import sys


def actions():
    """(id, title, keywords, argv, confirm) for every action available in this session."""
    session = os.environ.get("XDG_SESSION_ID")
    found = [("lock", "Lock screen", ("lock",), ["loginctl", "lock-session"], False)]
    if session:  # without a session id there is nothing to terminate
        found.append(("logout", "Log out", ("logout", "log out"), ["loginctl", "terminate-session", session], True))
    return found + [
        ("sleep", "Sleep", ("sleep", "suspend"), ["systemctl", "suspend"], False),
        ("restart", "Restart", ("restart", "reboot"), ["systemctl", "reboot"], True),
        ("shutdown", "Shut down", ("shutdown", "shut down", "poweroff"), ["systemctl", "poweroff"], True),
    ]


def results(text):
    text = text.strip().lower()
    if len(text) < 2:  # one letter would match half the actions on every keystroke
        return []
    return [{"id": id, "title": title, "subtitle": " ".join(argv), "score": 700,
             "action": "launch", "argv": argv, "confirm": confirm}
            for id, title, keywords, argv, confirm in actions()
            if any(k.startswith(text) for k in keywords)]


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
