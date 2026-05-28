#!/usr/bin/env python3
"""Keep native-port work from ending without updating the migration plan."""

import json
import os
import re
import subprocess
import sys


PLAN_PATH = "Native-Port-Plan.md"
NATIVE_SIGNAL_RE = re.compile(
    r"(native|macos|osx|linux|platformio|cmake|esp32|esp8266|esp hardware|"
    r"hardware support|native port|native-port)",
    re.IGNORECASE,
)
NATIVE_PATH_RE = re.compile(
    r"(^|/)(native|cmake|CMakeLists\.txt|scripts|tools|wled00|usermods|docs|"
    r"README\.md|AGENTS\.md|platformio\.ini|pio-scripts|\.github)(/|$)",
    re.IGNORECASE,
)


def git_output(cwd, *args):
    return subprocess.run(
        ["git", *args],
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    ).stdout


def status_paths(cwd):
    output = git_output(cwd, "status", "--porcelain", "--untracked-files=all")
    paths = []
    for line in output.splitlines():
        if len(line) < 4:
            continue
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        paths.append(path)
    return paths


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if payload.get("stop_hook_active"):
        return 0

    cwd = payload.get("cwd") if isinstance(payload.get("cwd"), str) else os.getcwd()
    paths = status_paths(cwd)
    if not paths or PLAN_PATH in paths:
        return 0

    last_message = payload.get("last_assistant_message", "")
    native_message = isinstance(last_message, str) and NATIVE_SIGNAL_RE.search(last_message)
    native_paths = [path for path in paths if NATIVE_PATH_RE.search(path)]
    if not native_message and not native_paths:
        return 0

    reason = (
        "Native-port-related work appears to have changed repository files without "
        "updating Native-Port-Plan.md. Before finalizing, update the relevant task "
        "section with actions taken, discrepancies or deviations, key decisions "
        "and reasoning, verification performed, docs updates, and any newly "
        "discovered tasks or risks. Then rerun the meaningful verification."
    )
    print(json.dumps({"decision": "block", "reason": reason}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
