#!/usr/bin/env bash
set -euo pipefail

PACKAGE_DIR="${AEROCORE_PACKAGE_DIR:?Set AEROCORE_PACKAGE_DIR to the built portable bundle}"
INSTALL_DIR="${AEROCORE_INSTALL_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/aerocore-browser}"
PACKAGE_VERSION="${AEROCORE_BROWSER_VERSION:-0.1.0}"
STEAM_SHORTCUT_HELPER="${AEROCORE_STEAM_SHORTCUT_HELPER:-${PACKAGE_DIR}/usr/bin/aerocore-steam-shortcut}"
VERSION_FILE="${INSTALL_DIR}/.aerocore-browser-version"
FIRST_INSTALL=0
if [[ ! -d "${INSTALL_DIR}" || ! -f "${VERSION_FILE}" ]]; then
    FIRST_INSTALL=1
fi

for required in aerocore-browser run.sh AppRun manifest.yaml; do
    [[ -e "${PACKAGE_DIR}/${required}" ]] || {
        printf 'Missing install payload file: %s\n' "${PACKAGE_DIR}/${required}" >&2
        exit 1
    }
done
[[ -x "${PACKAGE_DIR}/aerocore-browser" && -x "${PACKAGE_DIR}/run.sh" \
    && -x "${PACKAGE_DIR}/AppRun" ]] || {
    printf 'Install payload executables are not executable.\n' >&2
    exit 1
}

version_is_less() {
    local -a left_parts right_parts
    local left_part right_part index
    IFS=. read -r -a left_parts <<<"$1"
    IFS=. read -r -a right_parts <<<"$2"
    for ((index = 0; index < 3; index++)); do
        left_part="${left_parts[index]:-0}"
        right_part="${right_parts[index]:-0}"
        [[ "${left_part}" =~ ^[0-9]+$ && "${right_part}" =~ ^[0-9]+$ ]] || {
            printf 'Invalid version: %s or %s\n' "$1" "$2" >&2
            exit 2
        }
        if ((10#${left_part} < 10#${right_part})); then
            return 0
        elif ((10#${left_part} > 10#${right_part})); then
            return 1
        fi
    done
    return 1
}

CURRENT_VERSION=""
if [[ -f "${VERSION_FILE}" ]]; then
    CURRENT_VERSION="$(tr -d '[:space:]' <"${VERSION_FILE}")"
elif [[ -d "${INSTALL_DIR}" ]]; then
    CURRENT_VERSION="0.0.0"
fi
if [[ -n "${CURRENT_VERSION}" ]] && version_is_less "${PACKAGE_VERSION}" "${CURRENT_VERSION}"; then
    printf 'Refusing to downgrade %s from %s to %s.\n' \
        "${INSTALL_DIR}" "${CURRENT_VERSION}" "${PACKAGE_VERSION}" >&2
    exit 3
fi

steam_is_running() {
    pgrep -x steam >/dev/null 2>&1 || pgrep -x steamwebhelper >/dev/null 2>&1
}

run_on_host() {
    if command -v distrobox-host-exec >/dev/null 2>&1; then
        distrobox-host-exec "$@"
    else
        "$@"
    fi
}

close_steam() {
    local attempt pids pid

    if command -v steam >/dev/null 2>&1 || command -v distrobox-host-exec >/dev/null 2>&1; then
        run_on_host steam -shutdown >/dev/null 2>&1 || true
    fi
    for ((attempt = 0; attempt < 15; attempt++)); do
        steam_is_running || return 0
        sleep 1
    done

    pids="$( { pgrep -x steam || true; pgrep -x steamwebhelper || true; } | sort -u )"
    while IFS= read -r pid; do
        [[ -n "${pid}" ]] && kill -TERM "${pid}" 2>/dev/null || true
    done <<<"${pids}"
    for ((attempt = 0; attempt < 5; attempt++)); do
        steam_is_running || return 0
        sleep 1
    done
    return 1
}

# CEF subprocesses use the same executable path. Refuse to replace the bundle
# while any browser process still has this installation's executable open.
for process in /proc/[0-9]*; do
    [[ -e "${process}/exe" ]] || continue
    running_executable="$(readlink -f "${process}/exe" 2>/dev/null || true)"
    if [[ "${running_executable}" == "${INSTALL_DIR}/aerocore-browser" ]]; then
        printf 'AeroCore Browser is running (PID %s); close it before installing.\n' \
            "${process##*/}" >&2
        exit 4
    fi
done

if [[ "${FIRST_INSTALL}" -eq 1 ]]; then
    [[ -x "${STEAM_SHORTCUT_HELPER}" ]] || {
        printf 'Missing Steam shortcut helper: %s\n' "${STEAM_SHORTCUT_HELPER}" >&2
        exit 1
    }
    if steam_is_running; then
        if [[ ! -r /dev/tty ]]; then
            printf 'Steam is running. Re-run make install in an interactive terminal to confirm closing Steam for shortcut registration.\n' >&2
            exit 5
        fi
        answer=""
        read -r -p 'Steam is running. Close Steam and register the browser shortcut? [y/N] ' answer </dev/tty
        case "${answer}" in
            y|Y|yes|YES|Yes) ;;
            *) printf 'Installation cancelled.\n' >&2; exit 1 ;;
        esac
        close_steam || {
            printf 'Could not close Steam; refusing to write its shortcut database.\n' >&2
            exit 6
        }
    fi
fi

mkdir -p "${INSTALL_DIR}"
cp -a "${PACKAGE_DIR}/." "${INSTALL_DIR}/"

DESKTOP_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
mkdir -p "${DESKTOP_DIR}"
cat >"${DESKTOP_DIR}/io.aerocore.Browser.desktop" <<EOF
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
chmod 0644 "${DESKTOP_DIR}/io.aerocore.Browser.desktop"

if [[ "${FIRST_INSTALL}" -eq 1 ]]; then
    variant="$(sed -n 's/^variant: "\([^"]*\)"$/\1/p' "${PACKAGE_DIR}/manifest.yaml")"
    shortcut_name="汽核浏览器"
    [[ "${variant}" == "international" ]] && shortcut_name="AeroCore Browser"
    if ! LD_LIBRARY_PATH="${INSTALL_DIR}/lib:${INSTALL_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
        "${STEAM_SHORTCUT_HELPER}" \
        --name "${shortcut_name}" \
        --launcher "${INSTALL_DIR}/run.sh" \
        --icon "${INSTALL_DIR}/aerocore-browser-steam-icon.png" \
        --artwork-dir "${INSTALL_DIR}/usr/share/aerocore-browser/steam"; then
        printf 'Steam shortcut registration failed.\n' >&2
        exit 7
    fi
fi

printf '%s\n' "${PACKAGE_VERSION}" >"${VERSION_FILE}.tmp"
mv -f "${VERSION_FILE}.tmp" "${VERSION_FILE}"

printf 'Installed AeroCore Browser %s to %s\n' "${PACKAGE_VERSION}" "${INSTALL_DIR}"
if [[ "${FIRST_INSTALL}" -eq 1 ]]; then
    printf 'Open Steam to use the new non-Steam shortcut.\n'
else
    printf 'Steam was left running; its existing shortcut still targets this path.\n'
fi
