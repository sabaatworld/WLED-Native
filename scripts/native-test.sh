#!/usr/bin/env sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd -P)"
build_dir="$root/build/native"

"$root/scripts/native-build.sh"
ctest --test-dir "$build_dir" --output-on-failure
