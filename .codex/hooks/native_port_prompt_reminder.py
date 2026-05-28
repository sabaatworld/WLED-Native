#!/usr/bin/env python3
"""Add native-port process reminders to relevant Codex prompts."""

import json
import re
import sys


NATIVE_PROMPT_RE = re.compile(
    r"\b(native|macos|osx|linux|platformio|cmake|esp32|esp8266|esp hardware|"
    r"hardware support|native port|native-port)\b",
    re.IGNORECASE,
)

REMINDER = (
    "This prompt appears related to the WLED native macOS/Linux port. "
    "Treat Native-Port-Plan.md as the migration source of truth: update the "
    "relevant task log with actions taken, discrepancies or deviations, key "
    "decisions and reasoning, docs updates, test/build/browser verification, "
    "and any new tasks or risks discovered."
)


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    prompt = payload.get("prompt", "")
    if not isinstance(prompt, str) or not NATIVE_PROMPT_RE.search(prompt):
        return 0

    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "UserPromptSubmit",
            "additionalContext": REMINDER
        }
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
