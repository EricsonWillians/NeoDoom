#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VERSION_FILE="${PROJECT_ROOT}/src/version.h"

CURRENT_VERSION=""
NEW_VERSION=""

print_version_file_state() {
    local file="$1"
    echo "--- ${file}"
    awk '(
        /^#define VERSIONSTR / {
            print
            getline
            print
            getline
            print
            getline
            print
            getline
            print
            getline
            print
            exit
        }
    )' "${file}" || true
}

die() {
    echo "error: $*" >&2
    exit 1
}

usage() {
    cat <<'USAGE'
Usage:
  ./tools/bump-version.sh [--set X.Y.Z | --major | --minor | --patch] [--dry-run]

Options:
  --set X.Y.Z    Set a specific semantic version (for example 4.15.1)
  --major        Bump major version (X+1.0.0)
  --minor        Bump minor version (X.Y+1.0)
  --patch        Bump patch version (X.Y.Z+1)
  --dry-run      Show what would change without writing files

Examples:
  ./tools/bump-version.sh --patch
  ./tools/bump-version.sh --minor
  ./tools/bump-version.sh --set 4.15.1
USAGE
    exit 1
}

get_current_version() {
    local current
    current="$(grep -E '^#define VERSIONSTR ' "${VERSION_FILE}" | head -n1 | sed -E 's/^#define VERSIONSTR "([^"]*)"$/\1/' || true)"
    if [[ -z "${current}" ]]; then
        die "Could not read VERSIONSTR from ${VERSION_FILE}"
    fi
    echo "${current}"
}

parse_semver() {
    local value="$1"

    if [[ ! ${value} =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
        return 1
    fi

    echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]} ${BASH_REMATCH[3]}"
}

set_macros() {
    local version="$1"
    local major="$2"
    local minor="$3"
    local patch="$4"
    local dry_run="$5"

    if [[ "${dry_run}" == "true" ]]; then
        echo "Would set VERSIONSTR -> \"${version}\""
        echo "Would set RC_FILEVERSION -> ${major},${minor},${patch},0"
        echo "Would set RC_PRODUCTVERSION -> ${major},${minor},${patch},0"
        echo "Would set VER_MAJOR -> ${major}"
        echo "Would set VER_MINOR -> ${minor}"
        echo "Would set VER_REVISION -> ${patch}"
        return
    fi

    sed -i -E "s|^#define VERSIONSTR \\\"[^\\\"]*\\\"$|#define VERSIONSTR \\\"${version}\\\"|" "${VERSION_FILE}"
    sed -i -E "s/^#define RC_FILEVERSION .*/#define RC_FILEVERSION ${major},${minor},${patch},0/" "${VERSION_FILE}"
    sed -i -E "s/^#define RC_PRODUCTVERSION .*/#define RC_PRODUCTVERSION ${major},${minor},${patch},0/" "${VERSION_FILE}"
    sed -i -E "s/^#define VER_MAJOR [0-9]+/#define VER_MAJOR ${major}/" "${VERSION_FILE}"
    sed -i -E "s/^#define VER_MINOR [0-9]+/#define VER_MINOR ${minor}/" "${VERSION_FILE}"
    sed -i -E "s/^#define VER_REVISION [0-9]+/#define VER_REVISION ${patch}/" "${VERSION_FILE}"
    sed -i -E "s/^#define RC_PRODUCTVERSION2 .*/#define RC_PRODUCTVERSION2 VERSIONSTR/" "${VERSION_FILE}"
}

MODE="patch"
SET_VERSION=""
DRY_RUN="false"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --set)
            [[ $# -ge 2 ]] || usage
            SET_VERSION="$2"
            shift 2
            ;;
        --major)
            MODE="major"
            shift
            ;;
        --minor)
            MODE="minor"
            shift
            ;;
        --patch)
            MODE="patch"
            shift
            ;;
        --dry-run)
            DRY_RUN="true"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "Unknown argument: $1"
            ;;
    esac
done

CURRENT_VERSION="$(get_current_version)"

authors_note=""
if [[ -n "${SET_VERSION}" ]]; then
    if ! read -r CUR_MAJOR CUR_MINOR CUR_PATCH < <(parse_semver "${SET_VERSION}"); then
        die "Invalid --set version '${SET_VERSION}'. Expected X.Y.Z."
    fi
    NEW_VERSION="${SET_VERSION}"
    NEW_MAJOR="${CUR_MAJOR}"
    NEW_MINOR="${CUR_MINOR}"
    NEW_PATCH="${CUR_PATCH}"
else
    if ! read -r CUR_MAJOR CUR_MINOR CUR_PATCH < <(parse_semver "${CURRENT_VERSION}"); then
        die "Current VERSIONSTR '${CURRENT_VERSION}' is not semver (must be X.Y.Z)."
    fi

    NEW_MAJOR="${CUR_MAJOR}"
    NEW_MINOR="${CUR_MINOR}"
    NEW_PATCH="${CUR_PATCH}"

    case "${MODE}" in
        major)
            NEW_MAJOR=$((CUR_MAJOR + 1))
            NEW_MINOR=0
            NEW_PATCH=0
            ;;
        minor)
            NEW_MINOR=$((CUR_MINOR + 1))
            NEW_PATCH=0
            ;;
        patch)
            NEW_PATCH=$((CUR_PATCH + 1))
            ;;
        *)
            die "Unknown bump mode: ${MODE}"
            ;;
    esac

    NEW_VERSION="${NEW_MAJOR}.${NEW_MINOR}.${NEW_PATCH}"
fi

set_macros "${NEW_VERSION}" "${NEW_MAJOR}" "${NEW_MINOR}" "${NEW_PATCH}" "${DRY_RUN}"

if [[ "${DRY_RUN}" == "true" ]]; then
    echo "DRY RUN: no files changed."
    exit 0
fi

echo
print_version_file_state "${VERSION_FILE}"
echo

echo "Updated ${VERSION_FILE} to ${NEW_VERSION}"
echo "Next step:"
echo "  git add src/version.h"
echo "  git commit -m \"Bump BiasedDoom version to ${NEW_VERSION}\""
echo "  git tag v${NEW_VERSION}"
echo "  git push origin HEAD --tags"
echo
echo "Launcher visibility check: title and in-app version now use the new VERSIONSTR when no git description is available."
echo

echo "If you want a release entry, edit CHANGELOG.md before tagging."
