#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/native"

cmake -S "${repo_root}" -B "${build_dir}" "$@"
cmake --build "${build_dir}"
