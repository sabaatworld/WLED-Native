#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tmp_config_dir="$(mktemp -d)"

cleanup() {
  rm -rf "${tmp_config_dir}"
}

trap cleanup EXIT

"${repo_root}/scripts/native-build.sh"
"${repo_root}/scripts/native-run.sh" --help
"${repo_root}/scripts/native-run.sh" --version
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --resolve-path "/cfg.json"
printf '{"cli":true}\n' > "${tmp_config_dir}/cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --read-file "cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --copy-file "cli-test.json:cli-test-copy.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --rename-file "cli-test-copy.json:cli-test-renamed.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --compare-files "cli-test.json:cli-test-renamed.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --validate-json "cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --backup-file "cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --has-backup "cli-test.json"
printf '{"cli":false}\n' > "${tmp_config_dir}/cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --restore-file "cli-test.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --list-files
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --delete-file "cli-test-renamed.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --blend-color "FF0000:0000FF:128"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --fade-color "112233:128:1"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --prng-seq "4660:4"
printf '%s\n' '{"ps":[11,22,33],"dur":[1,1,1],"transition":[5,6,7],"repeat":1}' > "${tmp_config_dir}/playlist.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --playlist-run "playlist.json:3:150"
rm -f "${tmp_config_dir}/presets.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --init-presets
printf '%s\n' '{"0":{},"1":{"n":"Sunrise"},"2":{"n":"Evening"}}' > "${tmp_config_dir}/presets.json"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --preset-name "2"
"${repo_root}/scripts/native-run.sh" --config-dir "${tmp_config_dir}" --delete-preset "2"
