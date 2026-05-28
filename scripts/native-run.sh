#!/usr/bin/env sh
set -eu

root="$(cd "$(dirname "$0")/.." && pwd -P)"
build_dir="$root/build/native"
exe="$build_dir/wled-native"

if [ ! -x "$exe" ]; then
  "$root/scripts/native-build.sh"
fi

exec "$exe" "$@"
