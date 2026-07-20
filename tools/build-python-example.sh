#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
source_dir="${repo_root}/examples/python/hello_world"
output="${1:-${repo_root}/build/python-hello-world.pk3}"

mkdir -p "$(dirname "${output}")"
(
    cd "${source_dir}"
    cmake -E tar cf "${output}" --format=zip PYTHON ZSCRIPT pyscripts
)

printf 'Created %s\n' "${output}"
