#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/native"
binary="${build_dir}/wled-native"

if [[ ! -x "${binary}" ]]; then
  "${repo_root}/scripts/native-build.sh"
fi

exec "${binary}" "$@"
