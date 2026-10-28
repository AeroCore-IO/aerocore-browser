#!/usr/bin/env bash
# Maintainer-only helper: extract Widevine from a locally authorized Chrome
# package for development/testing. Runtime downloads use
# scripts/ensure-widevine-cdm.sh instead.
#
# Usage:
#   ./scripts/fetch-widevine-cdm.sh [--force] [--output <dir>]
#
# Do not use this script to create a redistributable browser package unless the
# applicable Widevine/Chrome terms explicitly permit that distribution.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Environment overrides
WIDEVINE_OUT_DIR="${WIDEVINE_OUT_DIR:-$ROOT_DIR/third_party/widevine/WidevineCdm}"
# dl.google.com is reachable directly on the supported build networks. Keep
# proxying opt-in for environments that explicitly need it.
WIDEVINE_PROXY_CMD="${WIDEVINE_PROXY_CMD-}"
WIDEVINE_DEB_URL="${WIDEVINE_DEB_URL:-https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb}"
WIDEVINE_ARCH="${WIDEVINE_ARCH:-x64}"
WIDEVINE_FORCE="${WIDEVINE_FORCE:-0}"

usage() {
    cat <<EOF
Usage: $(basename "$0") [--force] [--output <dir>]

Extract the Widevine CDM from Google Chrome's Linux deb into the selected
directory (default: third_party/widevine/WidevineCdm). This is a maintainer
utility and is not called by the runtime launcher.

Environment overrides:
  WIDEVINE_OUT_DIR     Destination directory (default: $WIDEVINE_OUT_DIR)
  WIDEVINE_PROXY_CMD   Optional network command wrapper (default: direct)
  WIDEVINE_DEB_URL     Chrome deb URL (default: $WIDEVINE_DEB_URL)
  WIDEVINE_ARCH        Target arch: x64 or arm64 (default: $WIDEVINE_ARCH)
  WIDEVINE_FORCE       Set to 1 to overwrite existing CDM (default: $WIDEVINE_FORCE)

Notes:
  - Downloads from dl.google.com directly by default; set WIDEVINE_PROXY_CMD
    only when the environment explicitly requires a proxy.
  - Widevine CDM is proprietary; redistribution may violate Google's EULA.
EOF
}

# Parse arguments
FORCE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --force) FORCE=1; shift ;;
        --output) WIDEVINE_OUT_DIR="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

# Check if already installed
if [[ -d "$WIDEVINE_OUT_DIR" && $FORCE -eq 0 ]]; then
    if [[ -f "$WIDEVINE_OUT_DIR/manifest.json" ]]; then
        echo ">>> Widevine CDM already present at $WIDEVINE_OUT_DIR (use --force to overwrite)"
        grep -E '"version"' "$WIDEVINE_OUT_DIR/manifest.json" || true
        exit 0
    fi
fi

# Create temp work directory
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# Download the maintainer input package.
ARCHIVE="$WORK_DIR/chrome.deb"
echo ">>> Downloading Chrome deb from $WIDEVINE_DEB_URL"
if [[ -n "$WIDEVINE_PROXY_CMD" ]]; then
    $WIDEVINE_PROXY_CMD curl -fL --retry 3 -o "$ARCHIVE" "$WIDEVINE_DEB_URL"
else
    curl -fL --retry 3 -o "$ARCHIVE" "$WIDEVINE_DEB_URL"
fi

# Extract deb
echo ">>> Extracting deb..."
cd "$WORK_DIR"
ar x chrome.deb

if [[ -f data.tar.xz ]]; then
    tar -xJf data.tar.xz
elif [[ -f data.tar.zst ]]; then
    tar --zstd -xf data.tar.zst
elif [[ -f data.tar.gz ]]; then
    tar -xzf data.tar.gz
else
    echo "Error: unexpected deb payload" >&2
    ls -la "$WORK_DIR"
    exit 1
fi

# Verify WidevineCdm exists
SRC_DIR="$WORK_DIR/opt/google/chrome/WidevineCdm"
if [[ ! -d "$SRC_DIR" ]]; then
    echo "Error: WidevineCdm not found in deb" >&2
    exit 1
fi

echo ">>> Source CDM:"
ls -la "$SRC_DIR"
echo ">>> Manifest:"
cat "$SRC_DIR/manifest.json"

# Verify architecture
case "$WIDEVINE_ARCH" in
    x64)   PLATFORM_DIR="linux_x64" ;;
    arm64) PLATFORM_DIR="linux_arm64" ;;
    *) echo "Error: unsupported arch $WIDEVINE_ARCH" >&2; exit 1 ;;
esac

if [[ ! -f "$SRC_DIR/_platform_specific/$PLATFORM_DIR/libwidevinecdm.so" ]]; then
    echo "Error: expected binary at _platform_specific/$PLATFORM_DIR/libwidevinecdm.so" >&2
    exit 1
fi

# Install
mkdir -p "$(dirname "$WIDEVINE_OUT_DIR")"
rm -rf "$WIDEVINE_OUT_DIR"
cp -a "$SRC_DIR" "$WIDEVINE_OUT_DIR"

echo ">>> Installed Widevine CDM to $WIDEVINE_OUT_DIR"
echo ">>> Version:"
grep -E '"version"' "$WIDEVINE_OUT_DIR/manifest.json"
echo ">>> Contents:"
find "$WIDEVINE_OUT_DIR" -type f -exec ls -la {} \;
