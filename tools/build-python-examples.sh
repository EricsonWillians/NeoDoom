#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Package the embedded-Python example suite as individual PK3 files.

Usage:
  ./tools/build-python-examples.sh [--output-dir PATH] [EXAMPLE ...]

Without EXAMPLE names, every directory under examples/python containing a
root PYTHON manifest is built. The default output is build/python-examples/.
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
examples_root="${repo_root}/examples/python"
output_dir="${repo_root}/build/python-examples"
requested=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --output-dir)
            [[ $# -ge 2 ]] || { printf 'error: --output-dir requires a path\n' >&2; exit 2; }
            output_dir="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            printf 'error: unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
        *)
            requested+=("$1")
            shift
            ;;
    esac
done

if [[ ${#requested[@]} -eq 0 ]]; then
    while IFS= read -r directory; do
        [[ -f "${directory}/PYTHON" ]] || continue
        requested+=("$(basename "${directory}")")
    done < <(find "${examples_root}" -mindepth 1 -maxdepth 1 -type d -print | sort)
fi

[[ ${#requested[@]} -gt 0 ]] || { printf 'error: no Python examples were found\n' >&2; exit 1; }
mkdir -p "${output_dir}"
output_dir="$(cd "${output_dir}" && pwd)"

built=0
for name in "${requested[@]}"; do
    [[ "${name}" =~ ^[A-Za-z0-9][A-Za-z0-9_-]*$ ]] || {
        printf 'error: invalid example name: %s\n' "${name}" >&2
        exit 2
    }
    source_dir="${examples_root}/${name}"
    [[ -f "${source_dir}/PYTHON" ]] || {
        printf 'error: example has no root PYTHON manifest: %s\n' "${name}" >&2
        exit 1
    }
    [[ -d "${source_dir}/python" ]] || {
        printf 'error: example has no python directory: %s\n' "${name}" >&2
        exit 1
    }

    package_entries=(PYTHON python)
    [[ -f "${source_dir}/ZSCRIPT" ]] && package_entries+=(ZSCRIPT)
    output="${output_dir}/${name}.pk3"
    (
        cd "${source_dir}"
        cmake -E tar cf "${output}" --format=zip "${package_entries[@]}"
    )
    printf 'Created %s\n' "${output}"
    ((built += 1))
done

suffix=s
[[ ${built} -eq 1 ]] && suffix=
printf 'Built %d Python example package%s in %s\n' \
    "${built}" "${suffix}" "${output_dir}"
