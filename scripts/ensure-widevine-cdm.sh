#!/usr/bin/env bash
# Make a Widevine CDM available for a future CEF start.
#
# The CDM is downloaded from the configured AeroCore mirror only after the
# browser UI is available. It is never stored in the source tree or sent
# through AeroCore services.

set -u

OUTPUT_DIR="${WIDEVINE_OUT_DIR:-}"
DOWNLOAD_URL="${WIDEVINE_RUNTIME_URL:-https://downloads.aerocore.io/widevine/1.4.9.1088-linux-x64.zip}"
CONNECT_TIMEOUT_SECONDS="${WIDEVINE_CONNECT_TIMEOUT_SECONDS:-5}"
MAX_TIME_SECONDS="${WIDEVINE_MAX_TIME_SECONDS:-30}"

log() { printf '[widevine] %s\n' "$*" >&2; }

valid_cdm() {
    local dir="$1"
    [[ -f "$dir/manifest.json" \
        && -f "$dir/_platform_specific/linux_x64/libwidevinecdm.so" ]]
}

copy_steam_cdm() {
    local source="$1" target="$2"
    [[ -f "$source/manifest.json" ]] || return 1
    local binary="$source/libwidevinecdm.so"
    if [[ ! -f "$binary" && -f "$source/linux-x64/libwidevinecdm.so" ]]; then
        binary="$source/linux-x64/libwidevinecdm.so"
    fi
    if [[ ! -f "$binary" && -f "$source/_platform_specific/linux_x64/libwidevinecdm.so" ]]; then
        binary="$source/_platform_specific/linux_x64/libwidevinecdm.so"
    fi
    [[ -f "$binary" ]] || return 1
    mkdir -p "$target/_platform_specific/linux_x64"
    cp -f "$source/manifest.json" "$target/manifest.json"
    cp -f "$binary" "$target/_platform_specific/linux_x64/libwidevinecdm.so"
    valid_cdm "$target"
}

find_steam_cdm() {
    local root
    local roots=(
        "${STEAM_PATH:-}"
        "${XDG_DATA_HOME:-${HOME}/.local/share}/Steam"
        "${HOME}/.steam/steam"
        "${HOME}/.steam/root"
        "${HOME}/.var/app/com.valvesoftware.Steam/.local/share/Steam"
    )
    for root in "${roots[@]}"; do
        [[ -n "$root" ]] || continue
        candidate="$root/config/widevine/linux-x64"
        if [[ -f "$candidate/libwidevinecdm.so" && -f "$root/config/widevine/manifest.json" ]]; then
            printf '%s\n' "$root/config/widevine"
            return 0
        fi
        for candidate in "$root/config/cefdata/WidevineCdm"/*; do
            if [[ -f "$candidate/manifest.json" \
                && -f "$candidate/_platform_specific/linux_x64/libwidevinecdm.so" ]]; then
                printf '%s\n' "$candidate"
                return 0
            fi
        done
    done
    return 1
}

find_cached_cdm() {
    local profile_root="${AEROCORE_CEF_PROFILE_DIR:-}"
    local candidate
    [[ -n "$profile_root" ]] || return 1
    for candidate in "$profile_root/WidevineCdm"/*; do
        if [[ -f "$candidate/manifest.json" \
            && -f "$candidate/_platform_specific/linux_x64/libwidevinecdm.so" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

if [[ -z "$OUTPUT_DIR" ]]; then
    printf 'Usage: WIDEVINE_OUT_DIR=/path/to/WidevineCdm %s\n' "$0" >&2
    exit 2
fi

if valid_cdm "$OUTPUT_DIR"; then
    exit 0
fi

if steam_cdm="$(find_steam_cdm 2>/dev/null)" && copy_steam_cdm "$steam_cdm" "$OUTPUT_DIR"; then
    log "Using Widevine CDM from Steam: $steam_cdm"
    exit 0
fi

if cached_cdm="$(find_cached_cdm 2>/dev/null)" && [[ "$cached_cdm" != "$OUTPUT_DIR" ]] \
    && copy_steam_cdm "$cached_cdm" "$OUTPUT_DIR"; then
    log "Using cached Widevine CDM: $cached_cdm"
    exit 0
fi

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT
archive="$work_dir/widevine.zip"
log "Downloading Widevine CDM from $DOWNLOAD_URL"
if ! curl -fL --connect-timeout "$CONNECT_TIMEOUT_SECONDS" --max-time "$MAX_TIME_SECONDS" \
    --retry 2 --retry-delay 2 --retry-max-time "$MAX_TIME_SECONDS" \
    --output "$archive" "$DOWNLOAD_URL"; then
    log 'Widevine download failed; continuing without DRM support.'
    exit 0
fi

mkdir -p "$work_dir/unpacked"
if ! unzip -q "$archive" -d "$work_dir/unpacked"; then
    log 'Widevine archive is invalid; continuing without DRM support.'
    exit 0
fi

source_dir="$work_dir/unpacked"
if [[ -d "$source_dir/WidevineCdm" ]]; then
    source_dir="$source_dir/WidevineCdm"
fi
if ! valid_cdm "$source_dir"; then
    # Steam's official ZIP uses linux-x64/ at the top level.
    mkdir -p "$work_dir/normalized/_platform_specific/linux_x64"
    if [[ -f "$source_dir/manifest.json" && -f "$source_dir/linux-x64/libwidevinecdm.so" ]]; then
        cp -f "$source_dir/manifest.json" "$work_dir/normalized/manifest.json"
        cp -f "$source_dir/linux-x64/libwidevinecdm.so" \
            "$work_dir/normalized/_platform_specific/linux_x64/libwidevinecdm.so"
        source_dir="$work_dir/normalized"
    fi
fi

if ! valid_cdm "$source_dir"; then
    log 'Widevine archive does not contain a usable Linux x64 CDM.'
    exit 0
fi

mkdir -p "$OUTPUT_DIR/_platform_specific/linux_x64"
cp -f "$source_dir/manifest.json" "$OUTPUT_DIR/manifest.json"
cp -f "$source_dir/_platform_specific/linux_x64/libwidevinecdm.so" \
    "$OUTPUT_DIR/_platform_specific/linux_x64/libwidevinecdm.so"
log "Widevine CDM installed at $OUTPUT_DIR"
