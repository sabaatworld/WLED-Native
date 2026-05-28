#!/usr/bin/env sh
set -eu

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <wled-native-executable>" >&2
  exit 64
fi

exe="$1"

help_output="$("$exe" --help)"
version_output="$("$exe" --version)"

printf '%s\n' "$help_output" | grep -F "WLED native runtime" >/dev/null
printf '%s\n' "$help_output" | grep -F "Usage: wled-native" >/dev/null
printf '%s\n' "$help_output" | grep -F -- "--config-dir <path>" >/dev/null
printf '%s\n' "$help_output" | grep -F -- "--host <address>" >/dev/null
printf '%s\n' "$help_output" | grep -F -- "--port <port>" >/dev/null
printf '%s\n' "$help_output" | grep -F -- "--log-level <level>" >/dev/null
printf '%s\n' "$version_output" | grep -F "WLED 17.0.0-dev" >/dev/null
