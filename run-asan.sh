#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export APP_PATH="${SCRIPT_DIR}/build-asan/aerocore-browser"

exec "${SCRIPT_DIR}/run.sh" "$@"