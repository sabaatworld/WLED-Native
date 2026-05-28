#!/usr/bin/env sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd -P)"
build_dir="$root/build/native"

cmake -S "$root" -B "$build_dir"
cmake --build "$build_dir"
