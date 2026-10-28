#!/usr/bin/env bash
set -euo pipefail

APPDIR="${AEROCORE_APPIMAGE_DIR:-${APPDIR:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}}"
if [[ -r "${APPDIR}/usr/share/aerocore-browser/widevine-cdm.env" ]]; then
    # shellcheck disable=SC1091
    source "${APPDIR}/usr/share/aerocore-browser/widevine-cdm.env"
fi
DEFAULT_INSTALL_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/aerocore-browser"
INSTALL_DIR="${AEROCORE_INSTALL_DIR:-${DEFAULT_INSTALL_DIR}}"
PACKAGE_VERSION="${AEROCORE_BROWSER_VERSION:-0.1.0}"
STEAM_SHORTCUT_NAME="汽核浏览器"
if grep -Fqx 'variant: "international"' "${APPDIR}/manifest.yaml" 2>/dev/null; then
    STEAM_SHORTCUT_NAME="AeroCore Browser"
fi

notify_info() {
    local message="$1"
    if command -v kdialog >/dev/null 2>&1; then
        kdialog --title "AeroCore Browser" --passivepopup "$message" 5 >/dev/null 2>&1 || true
    elif command -v zenity >/dev/null 2>&1; then
        zenity --notification --text="$message" >/dev/null 2>&1 || true
    elif command -v notify-send >/dev/null 2>&1; then
        notify-send "AeroCore Browser" "$message" >/dev/null 2>&1 || true
    fi
    printf '%s\n' "$message"
}

notify_error() {
    local message="$1"
    if command -v kdialog >/dev/null 2>&1; then
        kdialog --title "AeroCore Browser" --error "$message" >/dev/null 2>&1 || true
    elif command -v zenity >/dev/null 2>&1; then
        zenity --error --title="AeroCore Browser" --text="$message" >/dev/null 2>&1 || true
    elif command -v notify-send >/dev/null 2>&1; then
        notify-send --urgency=critical "AeroCore Browser" "$message" >/dev/null 2>&1 || true
    fi
    printf '%s\n' "$message" >&2
}

usage() {
    printf 'Install or uninstall AeroCore Browser for the current user.\n\n'
    printf 'Usage: %s [--install-dir DIR] [--uninstall|--status]\n' "$(basename "$0")"
}

version_is_less() {
    local left right left_part right_part i
    IFS=. read -r -a left_parts <<<"$1"
    IFS=. read -r -a right_parts <<<"$2"
    for ((i = 0; i < 3; i++)); do
        left_part="${left_parts[i]:-0}"
        right_part="${right_parts[i]:-0}"
        if ((10#$left_part < 10#$right_part)); then
            return 0
        elif ((10#$left_part > 10#$right_part)); then
            return 1
        fi
    done
    return 1
}

installed_version() {
    if [[ -f "${VERSION_FILE}" ]]; then
        tr -d '[:space:]' <"${VERSION_FILE}"
    elif [[ -d "${INSTALL_DIR}" ]]; then
        printf '0.0.0'
    fi
}

stop_steam() {
    local pids attempt

    if command -v steam >/dev/null 2>&1; then
        steam -shutdown >/dev/null 2>&1 || true
    fi

    for attempt in {1..10}; do
        pids="$(
            {
                pgrep -x steam || true
                pgrep -x steamwebhelper || true
            } | sort -u
        )"
        if [[ -z "${pids}" ]]; then
            return 0
        fi
        while IFS= read -r pid; do
            [[ -n "${pid}" ]] && kill -TERM "${pid}" 2>/dev/null || true
        done <<<"${pids}"
        sleep 1
    done

    pids="$(
        {
            pgrep -x steam || true
            pgrep -x steamwebhelper || true
        } | sort -u
    )"
    if [[ -n "${pids}" ]]; then
        while IFS= read -r pid; do
            [[ -n "${pid}" ]] && kill -KILL "${pid}" 2>/dev/null || true
        done <<<"${pids}"
        sleep 1
    fi

    {
        pgrep -x steam || true
        pgrep -x steamwebhelper || true
    } | grep -q . && return 1
    return 0
}

UNINSTALL=0
STATUS_ONLY=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-dir)
            [[ $# -ge 2 ]] || { printf '%s\n' '--install-dir requires a path' >&2; exit 2; }
            INSTALL_DIR="$2"
            shift 2
            ;;
        --uninstall)
            UNINSTALL=1
            shift
            ;;
        --status)
            STATUS_ONLY=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

VERSION_FILE="${INSTALL_DIR}/.aerocore-browser-version"
CURRENT_INSTALLED_VERSION="$(installed_version)"
if [[ ${STATUS_ONLY} -eq 1 ]]; then
    if [[ -n "${CURRENT_INSTALLED_VERSION}" ]]; then
        if version_is_less "${PACKAGE_VERSION}" "${CURRENT_INSTALLED_VERSION}"; then
            action=blocked
        elif version_is_less "${CURRENT_INSTALLED_VERSION}" "${PACKAGE_VERSION}"; then
            action=upgrade
        else
            action=reinstall
        fi
        printf 'installed=1\nversion=%s\naction=%s\ninstall_dir=%s\n' \
            "${CURRENT_INSTALLED_VERSION}" "${action}" "${INSTALL_DIR}"
    else
        printf 'installed=0\nversion=\naction=install\ninstall_dir=%s\n' "${INSTALL_DIR}"
    fi
    exit 0
fi

if [[ ${UNINSTALL} -eq 1 ]]; then
    if [[ -z "${CURRENT_INSTALLED_VERSION}" ]]; then
        notify_info "AeroCore Browser is not installed."
        exit 0
    fi
    notify_info "Uninstalling AeroCore Browser from ${INSTALL_DIR}..."
    rm -rf -- "${INSTALL_DIR}"
    rm -f -- "${DESKTOP_FILE:-${XDG_DATA_HOME:-${HOME}/.local/share}/applications/io.aerocore.Browser.desktop}"
    notify_info "AeroCore Browser was uninstalled."
    exit 0
fi

if [[ -n "${CURRENT_INSTALLED_VERSION}" ]] && version_is_less "${PACKAGE_VERSION}" "${CURRENT_INSTALLED_VERSION}"; then
    notify_error "A newer AeroCore Browser (${CURRENT_INSTALLED_VERSION}) is already installed; refusing to downgrade to ${PACKAGE_VERSION}."
    exit 3
fi

SOURCE_LAUNCHER="${APPDIR}/run.sh"
INSTALLED_APP_RUN="${APPDIR}/InstalledAppRun"
[[ -x "${SOURCE_LAUNCHER}" ]] || { printf 'Missing browser launcher: %s\n' "${SOURCE_LAUNCHER}" >&2; exit 1; }
[[ -x "${INSTALLED_APP_RUN}" ]] || { printf 'Missing installed AppRun: %s\n' "${INSTALLED_APP_RUN}" >&2; exit 1; }

INSTALL_PARENT="$(dirname -- "${INSTALL_DIR}")"
DESKTOP_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
DESKTOP_FILE="${DESKTOP_DIR}/io.aerocore.Browser.desktop"
STAGING_DIR="$(mktemp -d "${TMPDIR:-/tmp}/aerocore-browser-install.XXXXXX")"
trap 'rm -rf -- "${STAGING_DIR}"' EXIT

notify_info "Installing AeroCore Browser to ${INSTALL_DIR}..."
if ! {
    if ! stop_steam; then
        printf 'Steam could not be stopped; refusing to update its shortcut database.\n' >&2
        exit 1
    fi

    mkdir -p "${STAGING_DIR}" "${INSTALL_PARENT}" "${DESKTOP_DIR}"
    cp -a "${APPDIR}/." "${STAGING_DIR}/"
    install -m 0755 "${INSTALLED_APP_RUN}" "${STAGING_DIR}/AppRun"
    rm -f "${STAGING_DIR}/InstalledAppRun" "${STAGING_DIR}/usr/bin/aerocore-browser-installer"

    mkdir -p "${INSTALL_DIR}"
    cp -a "${STAGING_DIR}/." "${INSTALL_DIR}/"
    printf '%s\n' "${PACKAGE_VERSION}" >"${INSTALL_DIR}/.aerocore-browser-version"

    cat >"${DESKTOP_FILE}" <<EOF
[Desktop Entry]
Type=Application
Name=AeroCore Browser
Comment=Controller-first browser for SteamOS Gaming Mode
Exec="${INSTALL_DIR}/run.sh"
Icon=${INSTALL_DIR}/io.aerocore.Browser.svg
Terminal=false
Categories=Network;WebBrowser;
StartupNotify=true
EOF
    chmod 0644 "${DESKTOP_FILE}"

    STEAM_HELPER="${APPDIR}/usr/bin/aerocore-steam-shortcut"
    STEAM_ARTWORK_DIR="${APPDIR}/usr/share/aerocore-browser/steam"
    if [[ ! -x "${STEAM_HELPER}" ]]; then
        printf 'Warning: Steam shortcut helper is not available.\n' >&2
        exit 1
    elif ! "${STEAM_HELPER}" \
        --name "${STEAM_SHORTCUT_NAME}" \
        --launcher "${INSTALL_DIR}/run.sh" \
        --icon "${INSTALL_DIR}/aerocore-browser-steam-icon.png" \
        --artwork-dir "${STEAM_ARTWORK_DIR}"; then
        printf 'Steam shortcut registration failed.\n' >&2
        exit 1
    fi
}; then
    notify_error "AeroCore Browser installation failed."
    exit 1
fi

notify_info "AeroCore Browser installed successfully."
printf 'Desktop entry: %s\n' "${DESKTOP_FILE}"
printf 'Launch command: %s\n' "${INSTALL_DIR}/run.sh"
