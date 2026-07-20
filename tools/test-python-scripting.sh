#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Run the embedded-Python integration test against a real IWAD and game loop.

Usage:
  ./tools/test-python-scripting.sh --iwad PATH [options]

Options:
  --iwad PATH       Doom-compatible IWAD used to start the first map (required)
  --exe PATH        biaseddoom executable (default: build/biaseddoom)
  --timeout SEC     maximum seconds for each engine run (default: 45)
  --keep-temp       retain logs, config, savegame, and redirected output
  -h, --help        show this help

The BIASEDDOOM_TEST_IWAD environment variable can replace --iwad. When no
display is available, the script uses xvfb-run if it is installed.
USAGE
}

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
engine_exe="${repo_root}/build/biaseddoom"
iwad_path="${BIASEDDOOM_TEST_IWAD:-}"
test_timeout=45
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
        --timeout)
            [[ $# -ge 2 ]] || { printf 'error: --timeout requires seconds\n' >&2; exit 2; }
            test_timeout="$2"
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

[[ "${test_timeout}" =~ ^[1-9][0-9]*$ ]] || { printf 'error: --timeout must be a positive integer\n' >&2; exit 2; }
[[ -n "${iwad_path}" ]] || { printf 'error: --iwad PATH or BIASEDDOOM_TEST_IWAD is required\n' >&2; exit 2; }
[[ -f "${iwad_path}" ]] || { printf 'error: IWAD not found: %s\n' "${iwad_path}" >&2; exit 2; }
[[ -x "${engine_exe}" ]] || { printf 'error: executable not found or not executable: %s\n' "${engine_exe}" >&2; exit 2; }
command -v timeout >/dev/null 2>&1 || { printf 'error: GNU timeout is required\n' >&2; exit 2; }

test_root="$(mktemp -d "${TMPDIR:-/tmp}/biaseddoom-python-test.XXXXXX")"
cleanup() {
    if [[ "${keep_temp}" -eq 1 ]]; then
        printf 'Kept Python test artifacts at %s\n' "${test_root}"
    elif [[ -n "${test_root}" && -d "${test_root}" ]]; then
        rm -rf -- "${test_root}"
    fi
}
trap cleanup EXIT

"${script_dir}/build-python-example.sh"
example_pk3="${repo_root}/build/python-hello-world.pk3"
[[ -f "${example_pk3}" ]] || { printf 'error: example PK3 was not created\n' >&2; exit 1; }

if command -v python3 >/dev/null 2>&1; then
    PYTHONPYCACHEPREFIX="${test_root}/pycache" python3 -m py_compile \
        "${repo_root}/examples/python/hello_world/pyscripts/main.py" \
        "${repo_root}/examples/python/hello_world/pyscripts/helper.py" \
        "${repo_root}/examples/python/hello_world/pyscripts/autotest_failure.py"
fi

display_runner=()
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]] && command -v xvfb-run >/dev/null 2>&1; then
    display_runner=(xvfb-run -a)
fi

engine_dir="$(cd "$(dirname "${engine_exe}")" && pwd)"
engine_name="$(basename "${engine_exe}")"
active_log="${test_root}/active.log"
active_stdout="${test_root}/active.stdout"

printf 'Running active Python lifecycle/savegame test...\n'
set +e
(
    cd "${engine_dir}"
    env BIASEDDOOM_PYTHON_AUTOTEST=1 BIASEDDOOM_PYTHON_SAVE_AUTOTEST=1 \
        BIASEDDOOM_PYTHON_SAVE_FILE="${test_root}/saves/python-runtime-autotest.zds" \
        timeout --signal=INT --kill-after=5s "${test_timeout}s" \
        "${display_runner[@]}" "./${engine_name}" \
        -stdout -nosound -nointro -python -warp 1 \
        -config "${test_root}/biaseddoom.ini" \
        -savedir "${test_root}/saves" \
        -iwad "${iwad_path}" \
        -file "${example_pk3}" \
        +vid_activeinbackground true +i_pauseinbackground false \
        +logfile "${active_log}"
) >"${active_stdout}" 2>&1
active_status=$?
set -e

if [[ "${active_status}" -ne 0 ]]; then
    printf 'error: active engine run exited with status %d\n' "${active_status}" >&2
    tail -n 120 "${active_stdout}" >&2 || true
    exit 1
fi

required_markers=(
    "PYTEST engine_start"
    "PYTEST stdout_redirect"
    "PYTEST thread_guard=True"
    "PYTEST rollback_exception=intentional callback rollback test"
    "PYTEST map_load"
    "save=False"
    "PYTEST tick"
    "PYTEST actor_spawned tid=9901"
    "PYTEST actor_died tid=9901"
    "PYTEST actor_damaged tid=9901"
    "PYTEST filtered_spawn handle=True"
    "PYTEST actor_destroyed tid=9902"
    "PYTEST mutation tid=9901"
    "vfs_after_nested=True"
    "PYTEST realtime handle=True refs=True target=True velocity=True invalidated=True"
    "PYTEST zscript_bridge=True"
    "PYTEST budget_guard skipped=True disabled=True overruns=1"
    "PYTEST schedule_queued=True"
    "PYTEST schedule_ran"
    "sector=True line=True"
    "PYTEST performance profile=True budget=3 hard=True"
    "PYTEST save state="
    "PYTEST load restored_state="
    "save=True"
    "PYTEST autotest_complete"
    "PYTEST engine_shutdown"
    "pre_ticks="
    "post_ticks="
)

for marker in "${required_markers[@]}"; do
    if ! grep -Fq "${marker}" "${active_log}"; then
        printf 'error: active log is missing marker: %s\n' "${marker}" >&2
        tail -n 160 "${active_log}" >&2 || true
        exit 1
    fi
done

if grep -Fq "PYTEST rollback_leaked" "${active_log}"; then
    printf 'error: a callback from a failed helper import leaked into dispatch\n' >&2
    tail -n 160 "${active_log}" >&2 || true
    exit 1
fi

if ! find "${test_root}/saves" -type f -name 'python-runtime-autotest.*' -print -quit | grep -q .; then
    printf 'error: the savegame round trip did not create its test save\n' >&2
    exit 1
fi

inactive_log="${test_root}/inactive.log"
inactive_stdout="${test_root}/inactive.stdout"
printf 'Running trust opt-in rejection test...\n'
set +e
(
    cd "${engine_dir}"
    timeout --signal=INT --kill-after=5s "${test_timeout}s" \
        "${display_runner[@]}" "./${engine_name}" \
        -stdout -nosound -nointro \
        -config "${test_root}/inactive.ini" \
        -iwad "${iwad_path}" \
        -file "${example_pk3}" \
        +logfile "${inactive_log}" +quit
) >"${inactive_stdout}" 2>&1
inactive_status=$?
set -e

if [[ "${inactive_status}" -ne 0 ]]; then
    printf 'error: inactive engine run exited with status %d\n' "${inactive_status}" >&2
    tail -n 120 "${inactive_stdout}" >&2 || true
    exit 1
fi
if ! grep -Fq "found but not executed" "${inactive_log}"; then
    printf 'error: inactive run did not explain the Python trust opt-in\n' >&2
    tail -n 120 "${inactive_log}" >&2 || true
    exit 1
fi
if grep -Fq "PYTEST engine_start" "${inactive_log}"; then
    printf 'error: Python executed without -python or py_enabled\n' >&2
    exit 1
fi

printf 'PASS: Python startup, VFS import, native real-time handles/mutations, callbacks,\n'
printf '      JSON save/load, typed ZScript bridge, shutdown, and trust opt-in all passed.\n'
