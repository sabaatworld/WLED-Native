#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"${repo_root}/scripts/native-build.sh"
"${repo_root}/scripts/native-run.sh" --help
"${repo_root}/scripts/native-run.sh" --version
