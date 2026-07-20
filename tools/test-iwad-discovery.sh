#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Exercise Steam IWAD discovery with synthetic modern library metadata.

Usage:
  ./tools/test-iwad-discovery.sh --iwad PATH [--exe PATH] [--keep-temp]

Options:
  --iwad PATH    readable supported IWAD copied into the synthetic library
  --exe PATH     executable to test (default: build/biaseddoom)
  --keep-temp    retain metadata, copied IWAD, and logs
  -h, --help     show this help
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
engine_exe="${repo_root}/build/biaseddoom"
iwad_path="${BIASEDDOOM_TEST_IWAD:-}"
keep_temp=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --iwad)
            [[ $# -ge 2 ]] || { printf 'error: --iwad requires a path\n' >&2; exit 2; }
            iwad_path="$2"
            shift 2
            ;;
        --exe)
            [[ $# -ge 2 ]] || { printf 'error: --exe requires a path\n' >&2; exit 2; }
            engine_exe="$2"
            shift 2
            ;;
        --keep-temp)
            keep_temp=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'error: unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

[[ -n "${iwad_path}" ]] || { printf 'error: --iwad PATH or BIASEDDOOM_TEST_IWAD is required\n' >&2; exit 2; }
[[ -r "${iwad_path}" ]] || { printf 'error: IWAD is not readable: %s\n' "${iwad_path}" >&2; exit 2; }
[[ -x "${engine_exe}" ]] || { printf 'error: executable not found: %s\n' "${engine_exe}" >&2; exit 2; }

iwad_path="$(cd "$(dirname "${iwad_path}")" && pwd)/$(basename "${iwad_path}")"
engine_exe="$(cd "$(dirname "${engine_exe}")" && pwd)/$(basename "${engine_exe}")"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/biaseddoom-iwad-discovery.XXXXXX")"
cleanup() {
    if [[ "${keep_temp}" -eq 1 ]]; then
        printf 'Kept IWAD discovery artifacts at %s\n' "${test_root}"
    elif [[ -n "${test_root}" && -d "${test_root}" ]]; then
        rm -rf -- "${test_root}"
    fi
}
trap cleanup EXIT

steam_root="${test_root}/Portable Steam"
library_root="${test_root}/External Steam Library"
install_dir="Renamed Doom Installation"
iwad_dir="${library_root}/steamapps/common/${install_dir}/base/doom2"
synthetic_iwad="${iwad_dir}/DOOM2.WAD"
escaped_dir="${library_root}/Escaped Steam Metadata/base"
escaped_iwad="${escaped_dir}/DOOM2.WAD"
mkdir -p "${steam_root}/steamapps" "${library_root}/steamapps" "${iwad_dir}" "${escaped_dir}"
cp "${iwad_path}" "${synthetic_iwad}"
cp "${iwad_path}" "${escaped_iwad}"

cat > "${steam_root}/steamapps/libraryfolders.vdf" <<VDF
"libraryfolders"
{
    "0"
    {
        "path" "${steam_root}"
    }
    "1"
    {
        "path" "${library_root}"
        "apps"
        {
            "2280" "1"
        }
    }
}
VDF

cat > "${library_root}/steamapps/appmanifest_2300.acf" <<'VDF'
"AppState"
{
    "appid" "2300"
    "name" "Malformed synthetic manifest"
    "installdir" "../../Escaped Steam Metadata"
}
VDF

cat > "${library_root}/steamapps/appmanifest_2280.acf" <<VDF
"AppState"
{
    "appid" "2280"
    "name" "Synthetic DOOM + DOOM II"
    "installdir" "${install_dir}"
}
VDF

stdout_file="${test_root}/findiwads.stdout"
config_file="${test_root}/biaseddoom.ini"
engine_dir="$(dirname "${engine_exe}")"
engine_name="$(basename "${engine_exe}")"
(
    cd "${engine_dir}"
    STEAM_DIR="${steam_root}" "./${engine_name}" \
        -stdout -config "${config_file}" -findiwads
) > "${stdout_file}" 2>&1

grep -Fq 'IWAD discovery report' "${stdout_file}" || {
    printf 'error: discovery report was not printed\n' >&2
    tail -n 100 "${stdout_file}" >&2
    exit 1
}
grep -Fq "${synthetic_iwad}" "${stdout_file}" || {
    printf 'error: modern external Steam library/appmanifest path was not discovered\n' >&2
    tail -n 100 "${stdout_file}" >&2
    exit 1
}
if grep -Fq "${escaped_iwad}" "${stdout_file}"; then
    printf 'error: unsafe appmanifest installdir escaped steamapps/common\n' >&2
    tail -n 100 "${stdout_file}" >&2
    exit 1
fi

printf 'PASS: modern Steam library metadata, external library, safe renamed installdir, and IWAD validation succeeded.\n'
