#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUMP_SCRIPT="${SCRIPT_DIR}/bump-version.sh"

MODE="patch"
SET_VERSION=""
DRY_RUN="false"
SKIP_PUSH="false"
SKIP_TAG="false"
ALLOW_DIRTY="false"
REMOTE_NAME="origin"
LAUNCH_WORKFLOW="true"
DRAFT_RELEASE="false"
PRERELEASE_RELEASE="false"
MESSAGE="Bump BiasedDoom version to"

usage() {
    cat <<'USAGE'
Usage:
  ./tools/release.sh [--major|--minor|--patch|--set X.Y.Z] [options]

Options:
  --major        bump major version (X+1.0.0)
  --minor        bump minor version (X.Y+1.0)
  --patch        bump patch version (default)
  --set X.Y.Z    set a specific version
  --dry-run      show commands without touching git history
  --skip-push    skip pushing commits/tags
  --skip-tag     skip tagging (only commit)
  --allow-dirty  allow uncommitted changes before bump (not recommended)
  --remote NAME  git remote to push to (default: origin)
  --no-workflow  do not trigger a GitHub release workflow automatically
  --draft        mark release as draft when workflow is triggered
  --prerelease   mark release as prerelease when workflow is triggered
  --message TEXT custom git commit message
  -h, --help     show this help

Examples:
  ./tools/release.sh --minor
  ./tools/release.sh --set 4.15.1 --prerelease
  ./tools/release.sh --set 4.15.1 --draft
USAGE
    exit 1
}

fatal() {
    echo "error: $*" >&2
    exit 1
}

warn() {
    echo "warning: $*" >&2
}

get_version() {
    awk -F'"' '/^#define VERSIONSTR / {print $2; exit}' "${PROJECT_ROOT}/src/version.h"
}

require_clean_tree() {
    if [[ "${ALLOW_DIRTY}" == "true" ]]; then
        return 0
    fi

    if ! git diff --quiet || ! git diff --cached --quiet; then
        fatal "Working tree is not clean. Commit/stash local changes first or use --allow-dirty."
    fi
}

assert_remote() {
    if ! git remote get-url "${REMOTE_NAME}" >/dev/null 2>&1; then
        fatal "Remote '${REMOTE_NAME}' does not exist."
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
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
        --set)
            [[ $# -ge 2 ]] || usage
            SET_VERSION="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN="true"
            shift
            ;;
        --skip-push)
            SKIP_PUSH="true"
            shift
            ;;
        --skip-tag)
            SKIP_TAG="true"
            shift
            ;;
        --allow-dirty)
            ALLOW_DIRTY="true"
            shift
            ;;
        --remote)
            [[ $# -ge 2 ]] || usage
            REMOTE_NAME="$2"
            shift 2
            ;;
        --no-workflow)
            LAUNCH_WORKFLOW="false"
            shift
            ;;
        --draft)
            DRAFT_RELEASE="true"
            shift
            ;;
        --prerelease)
            PRERELEASE_RELEASE="true"
            shift
            ;;
        --message)
            [[ $# -ge 2 ]] || usage
            MESSAGE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            fatal "Unknown option: $1"
            ;;
    esac
done

if ! [[ -x "${BUMP_SCRIPT}" ]]; then
    fatal "Missing executable: ${BUMP_SCRIPT}"
fi

if [[ -n "${SET_VERSION}" ]]; then
    BUMP_CMD=("${BUMP_SCRIPT}" --set "${SET_VERSION}")
else
    BUMP_CMD=("${BUMP_SCRIPT}" "--${MODE}")
fi

if [[ "${DRY_RUN}" == "true" ]]; then
    echo "[release] Preview mode"
    echo "Bump command: ${BUMP_CMD[*]}"
    echo "No local files will be modified and no git writes will happen."
fi

if [[ "${DRY_RUN}" == "true" ]]; then
    "${BUMP_CMD[@]}" --dry-run
    exit 0
fi

cd "${PROJECT_ROOT}"

if [[ "${SKIP_PUSH}" == "false" ]]; then
    assert_remote
    if ! git symbolic-ref -q HEAD >/dev/null; then
        fatal "Release releases should run from a checked-out branch when pushing to remote."
    fi
    require_clean_tree
fi

echo "[release] Bumping version in source"
"${BUMP_CMD[@]}"

VERSION="$(get_version)"
if [[ -z "${VERSION}" ]]; then
    fatal "Could not read VERSIONSTR after bump."
fi

TAG="v${VERSION}"

if git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null 2>&1; then
    fatal "Tag ${TAG} already exists. Choose a different version first."
fi

if [[ -z "$(git status --short src/version.h)" ]]; then
    fatal "No change to src/version.h after bump."
fi

git add src/version.h
git commit -m "${MESSAGE} ${VERSION}"

if [[ "${SKIP_TAG}" == "false" ]]; then
    git tag "${TAG}"
    echo "Tagged ${TAG}."
else
    echo "Skipped tag creation (--skip-tag)."
fi

if [[ "${SKIP_PUSH}" == "false" ]]; then
    git push "${REMOTE_NAME}" HEAD
    if [[ "${SKIP_TAG}" == "false" ]]; then
        git push "${REMOTE_NAME}" "${TAG}"
    fi

        if [[ "${LAUNCH_WORKFLOW}" == "true" ]]; then
            if [[ "${SKIP_TAG}" == "true" ]]; then
                warn "Skipping workflow launch because --skip-tag was used and no local release tag was created."
                warn "Create/push tag ${TAG} first, then rerun without --skip-tag to trigger GitHub release."
            else
                if command -v gh >/dev/null 2>&1; then
                    if [[ "${SKIP_TAG}" == "false" || $(git tag -l "${TAG}") == "${TAG}" ]]; then
                        gh workflow run Release \
                            --field version="${VERSION}" \
                            --field release_draft="${DRAFT_RELEASE}" \
                            --field release_prerelease="${PRERELEASE_RELEASE}"
                        echo "Triggered workflow: Release for ${TAG}."
                    fi
                else
                    echo "Note: GitHub CLI not found; workflow will start automatically when push tag reaches GitHub."
                fi
        fi
    else
        warn "Release workflow auto-launch disabled by --no-workflow."
        warn "Open GitHub Actions → Release and run it manually with version ${VERSION}."
    fi
fi

echo "Release prep complete."
echo "Version: ${VERSION}"
echo "Tag: ${TAG}"
if [[ "${SKIP_PUSH}" == "true" ]]; then
    echo "Run: git push ${REMOTE_NAME} HEAD"
    if [[ "${SKIP_TAG}" == "false" ]]; then
        echo "      git push ${REMOTE_NAME} ${TAG}"
    fi
    echo "Then run the Release workflow from GitHub Actions, or locally:"
    echo "  gh workflow run Release --field version=${VERSION} --field release_draft=${DRAFT_RELEASE} --field release_prerelease=${PRERELEASE_RELEASE}"
fi
