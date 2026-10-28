#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_APP_PATH="${SCRIPT_DIR}/build/aerocore-browser"
ASAN_APP_PATH="${SCRIPT_DIR}/build-asan/aerocore-browser"

if [[ -z "${APP_PATH:-}" ]]; then
    APP_PATH="${DEFAULT_APP_PATH}"
fi
#APP_PATH="${SCRIPT_DIR}/dist/aerocore-browser-portable/aerocore-browser"
LOG_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/aerocore-browser"
LOG_FILE="${LOG_DIR}/launcher.log"
SANITIZER_LOG_PREFIX="${LOG_DIR}/sanitizer"
CACHE_BASE_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/AeroCore/AeroCore Browser"
CEF_PROFILE_DIR_DEFAULT="${CACHE_BASE_DIR}/cef-profile"
mkdir -p "${LOG_DIR}"

if [[ -z "${AEROCORE_CEF_PROFILE_DIR:-}" ]]; then
    export AEROCORE_CEF_PROFILE_DIR="${CEF_PROFILE_DIR_DEFAULT}"
fi

# CEF sees the CDM on the next launch. The preparation is started by the app
# after its UI is loaded so a slow download can never block Steam startup.
WIDEVINE_HELPER="${SCRIPT_DIR}/scripts/ensure-widevine-cdm.sh"
if [[ ! -x "${WIDEVINE_HELPER}" && -x "${SCRIPT_DIR}/usr/libexec/aerocore-browser-ensure-widevine-cdm" ]]; then
    WIDEVINE_HELPER="${SCRIPT_DIR}/usr/libexec/aerocore-browser-ensure-widevine-cdm"
fi
if [[ -x "${WIDEVINE_HELPER}" ]]; then
    if [[ -w "$(dirname "$APP_PATH")" ]]; then
        WIDEVINE_OUT_DIR="$(dirname "$APP_PATH")/WidevineCdm"
    else
        WIDEVINE_OUT_DIR="${AEROCORE_CEF_PROFILE_DIR}/WidevineCdm"
    fi
    export AEROCORE_WIDEVINE_DIR="$WIDEVINE_OUT_DIR"
    export AEROCORE_WIDEVINE_HELPER="$WIDEVINE_HELPER"
fi

reset_cef_profile_if_requested() {
    if [[ "${AEROCORE_RESET_CEF_PROFILE:-0}" != "1" ]]; then
        mkdir -p "${AEROCORE_CEF_PROFILE_DIR}"
        return
    fi

    log "Resetting CEF profile directory: ${AEROCORE_CEF_PROFILE_DIR}"
    rm -rf "${AEROCORE_CEF_PROFILE_DIR}"
    mkdir -p "${AEROCORE_CEF_PROFILE_DIR}"
}

timestamp() {
    date '+%F %T'
}

log() {
    printf '[%s] %s\n' "$(timestamp)" "$*" | tee -a "${LOG_FILE}" >&2
}

gamescope_session() {
    local value
    for value in \
        "${XDG_CURRENT_DESKTOP:-}" \
        "${XDG_SESSION_DESKTOP:-}" \
        "${DESKTOP_SESSION:-}"; do
        if [[ "${value,,}" == *gamescope* ]]; then
            return 0
        fi
    done

    if [[ -n "${GAMESCOPE_WAYLAND_DISPLAY:-}" || -n "${GAMESCOPE_SESSION:-}" ]]; then
        return 0
    fi

    return 1
}

if [[ ! -x "${APP_PATH}" ]]; then
    log "Missing executable: ${APP_PATH}"
    log "Run 'make' from ${SCRIPT_DIR} first."
    exit 1
fi

if [[ -z "${LANG:-}" || "${LANG}" == "C" || "${LANG}" == "POSIX" ]]; then
    export LANG="C.UTF-8"
fi
export LC_ALL="${LC_ALL:-${LANG}}"
export DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SYSTEM_BUS_ADDRESS:-unix:path=/run/dbus/system_bus_socket}"

# Homebrew's ld.so does not read /etc/ld.so.cache; the app needs system libs
# (libGLX, libudev) plus Homebrew's own glibc (matching the ld.so version).
# On hosts with a newer system glibc, link the app to the system loader so system
# libraries are not loaded into the older Homebrew runtime.
# IMPORTANT: only apply this to the app process itself. Exporting this globally
# would make system tools (mkdir/tee/date) load the wrong glibc and segfault.
BREW_PREFIX="${HOMEBREW_PREFIX:-/home/linuxbrew/.linuxbrew}"
BREW_GLIBC_DIR="$(ls -d "${BREW_PREFIX}/Cellar/glibc/"*/lib 2>/dev/null | sort -V | tail -n1)"
if [[ -z "${BREW_GLIBC_DIR}" ]]; then
    BREW_GLIBC_DIR="/home/linuxbrew/.linuxbrew/Cellar/glibc/2.39/lib"
fi
APP_LD_LIBRARY_PATH="${BREW_GLIBC_DIR}:/usr/lib"
APP_LAUNCHER=("${APP_PATH}")
SYSTEM_LD=""
for loader in /lib64/ld-linux-x86-64.so.2 /usr/lib/ld-linux-x86-64.so.2; do
    if [[ -x "${loader}" ]]; then
        SYSTEM_LD="${loader}"
        break
    fi
done
BREW_LD="${BREW_PREFIX}/lib/ld.so"
if [[ -n "${SYSTEM_LD}" && -x "${BREW_LD}" ]]; then
    SYSTEM_GLIBC_VERSION="$("${SYSTEM_LD}" --version 2>/dev/null \
        | sed -n '1{s/.*version \([0-9][0-9.]*[0-9]\).*/\1/p;}')"
    BREW_GLIBC_VERSION="$("${BREW_LD}" --version 2>/dev/null \
        | sed -n '1{s/.*version \([0-9][0-9.]*[0-9]\).*/\1/p;}')"
    if [[ -n "${SYSTEM_GLIBC_VERSION}" && -n "${BREW_GLIBC_VERSION}" ]] \
        && [[ "$(printf '%s\n' "${BREW_GLIBC_VERSION}" "${SYSTEM_GLIBC_VERSION}" | sort -V | tail -n1)" == "${SYSTEM_GLIBC_VERSION}" ]] \
        && [[ "${SYSTEM_GLIBC_VERSION}" != "${BREW_GLIBC_VERSION}" ]]; then
        APP_LD_LIBRARY_PATH=""
        if ! command -v readelf >/dev/null 2>&1; then
            log "Cannot verify the executable's dynamic loader; install binutils and rebuild with 'make'."
            exit 1
        fi
        APP_INTERPRETER="$(readelf -l "${APP_PATH}" \
            | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p' \
            | sed 's/[[:space:]]*$//')"
        if [[ -z "${APP_INTERPRETER}" ]] \
            || [[ "$(readlink -f "${APP_INTERPRETER}")" != "$(readlink -f "${SYSTEM_LD}")" ]]; then
            log "Executable uses ${APP_INTERPRETER:-an unknown dynamic loader}; run 'make' to relink it for system glibc ${SYSTEM_GLIBC_VERSION}."
            exit 1
        fi
        log "Using executable linked to system glibc ${SYSTEM_GLIBC_VERSION} instead of Homebrew glibc ${BREW_GLIBC_VERSION}."
    fi
fi

# Steam's gameoverlayrenderer.so (32-bit) in LD_PRELOAD cannot be loaded by
# this 64-bit process and produces noisy ld.so warnings.  The overlay is not
# needed for this app, so remove it.
unset LD_PRELOAD

BASE_CHROMIUM_FLAGS="${AEROCORE_CHROMIUM_FLAGS:-}"
GPU_MODE="${AEROCORE_GPU_MODE:-auto}"

export AEROCORE_SPIN_GUARD="${AEROCORE_SPIN_GUARD:-1}"
export AEROCORE_GLIB_IDLE_MS="${AEROCORE_GLIB_IDLE_MS:-2}"

#export AEROCORE_STALL_MS="${AEROCORE_STALL_MS:-50}"
#export AEROCORE_LOAD_TIMING="${AEROCORE_LOAD_TIMING:-1}"

if [[ -z "${ASAN_OPTIONS:-}" ]]; then
    export ASAN_OPTIONS="log_path=${SANITIZER_LOG_PREFIX}:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1:symbolize=1"
fi

if [[ -z "${UBSAN_OPTIONS:-}" ]]; then
    export UBSAN_OPTIONS="log_path=${SANITIZER_LOG_PREFIX}-ubsan:print_stacktrace=1:halt_on_error=1"
fi

clear_stale_cef_singleton() {
    local profile_dir="${AEROCORE_CEF_PROFILE_DIR}"
    local lock_path="${profile_dir}/SingletonLock"
    local lock_target lock_pid

    if [[ ! -L "${lock_path}" ]]; then
        return
    fi

    lock_target="$(readlink "${lock_path}" 2>/dev/null || true)"
    lock_pid="${lock_target##*-}"
    if [[ "${lock_pid}" =~ ^[0-9]+$ ]] && ! kill -0 "${lock_pid}" 2>/dev/null; then
        log "Removing stale CEF singleton lock from dead pid ${lock_pid}"
        rm -f "${profile_dir}/SingletonLock" "${profile_dir}/SingletonCookie" "${profile_dir}/SingletonSocket"
    fi
}

reset_cef_profile_if_requested
clear_stale_cef_singleton

find_running_main_instance() {
    local line pid cmd

    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        pid="${line%% *}"
        cmd="${line#* }"

        if [[ ! "${pid}" =~ ^[0-9]+$ ]]; then
            continue
        fi
        if [[ "${pid}" -eq $$ ]]; then
            continue
        fi
        if [[ "${cmd}" == *" --type="* ]]; then
            continue
        fi
        if [[ "${cmd}" == "${APP_PATH}" || "${cmd}" == "${APP_PATH} "* ]]; then
            printf '%s\n' "${pid}"
            return 0
        fi
    done < <(pgrep -af "${APP_PATH}" 2>/dev/null || true)

    return 1
}

has_windowed_arg() {
    local arg
    for arg in "$@"; do
        if [[ "${arg}" == "--windowed" ]]; then
            return 0
        fi
    done

    return 1
}

run_candidate() {
    local platform="$1"
    local extra_flags="$2"
    local note="$3"
    local candidate_gpu_mode="$4"
    local start_ts end_ts elapsed rc
    shift 4

    local -a flag_args=()
    local -a app_args=("$@")
    local suppress_xcb_keyboard="${AEROCORE_SUPPRESS_XCB_KEYBOARD:-}"
    local force_windowed="${AEROCORE_FORCE_WINDOWED:-0}"
    if [[ -n "${extra_flags}" ]]; then
        read -r -a flag_args <<<"${extra_flags}"
    fi
    if [[ "${force_windowed}" == "1" ]] && ! has_windowed_arg "${app_args[@]}"; then
        app_args=("--windowed" "${app_args[@]}")
    fi

    if [[ "${platform}" == "xcb" && -z "${suppress_xcb_keyboard}" ]]; then
        suppress_xcb_keyboard="1"
    fi

    log "Launching candidate platform=${platform} gpu-mode=${candidate_gpu_mode} ${note}"
    log "Session DISPLAY=${DISPLAY:-<unset>} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>} XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<unset>} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"

    local pre_log_size=0
    if [[ -f "${LOG_FILE}" ]]; then
        pre_log_size="$(stat -c %s "${LOG_FILE}" 2>/dev/null || echo 0)"
    fi

    start_ts="$(date +%s)"
    AEROCORE_GPU_MODE="${candidate_gpu_mode}" \
    AEROCORE_SUPPRESS_XCB_KEYBOARD="${suppress_xcb_keyboard}" \
    QT_QPA_PLATFORM="${platform}" \
    LD_LIBRARY_PATH="${APP_LD_LIBRARY_PATH}" \
    "${APP_PATH}" "${flag_args[@]}" "${app_args[@]}" >>"${LOG_FILE}" 2>&1
    rc=$?
    end_ts="$(date +%s)"
    elapsed=$((end_ts - start_ts))

    log "Candidate platform=${platform} exited rc=${rc} after ${elapsed}s"

    # Detect clean shutdown: the app prints "[shutdown] app.exec returned exitCode="
    # before CEF static destructors run. Those destructors can segfault and make
    # the kernel report rc=139 even though the user already confirmed exit.
    # If we see the marker in this candidate's portion of the log, treat as success.
    if [[ ${rc} -ne 0 ]]; then
        if tail -c +$((pre_log_size + 1)) "${LOG_FILE}" 2>/dev/null \
            | grep -q '\[shutdown\] app.exec returned exitCode='; then
            log "Candidate platform=${platform} performed clean shutdown before rc=${rc}; treating as exitCode=0."
            return 0
        fi
    fi

    if [[ ${rc} -eq 134 || ${rc} -eq 139 ]]; then
        log "Recent output before crash:"
        if tail -c +$((pre_log_size + 1)) "${LOG_FILE}" 2>/dev/null \
            | grep -q '\[crash\] fatal signal'; then
            tail -c +$((pre_log_size + 1)) "${LOG_FILE}" 2>/dev/null \
                | awk '/\[crash\] fatal signal/{flag=1} flag{print; if(/\[crash\] end of backtrace/) exit}' \
                | while IFS= read -r line; do
                    [[ -n "${line}" ]] || continue
                    log "  ${line}"
                done
        else
            tail -c +$((pre_log_size + 1)) "${LOG_FILE}" 2>/dev/null | tail -n 30 | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        fi
        if [[ -f /tmp/aerocore-browser-crash.log ]]; then
            log "Contents of /tmp/aerocore-browser-crash.log:"
            tail -n 80 /tmp/aerocore-browser-crash.log | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        fi
        if command -v coredumpctl >/dev/null 2>&1; then
            log "coredumpctl info (latest, if any):"
            local cd_out
            cd_out="$(coredumpctl info -1 2>/dev/null | head -n 80 || true)"
            if [[ -n "${cd_out}" ]]; then
                while IFS= read -r line; do
                    [[ -n "${line}" ]] || continue
                    log "  ${line}"
                done <<<"${cd_out}"
            else
                log "  (no matching coredump)"
            fi
        fi
    fi

    return "${rc}"
}

if [[ -n "${AEROCORE_FORCE_PLATFORM:-}" ]]; then
    log "AEROCORE_FORCE_PLATFORM=${AEROCORE_FORCE_PLATFORM}"
    log "AEROCORE_GPU_MODE=${GPU_MODE}"
    run_candidate "${AEROCORE_FORCE_PLATFORM}" "${BASE_CHROMIUM_FLAGS} --ozone-platform-hint=auto" "forced" "${GPU_MODE}" "$@"
    exit $?
fi

log "Launcher started. Log file: ${LOG_FILE}"
log "Configured GPU mode: ${GPU_MODE}"
log "Resolved app path: ${APP_PATH}"
log "Sanitizer log prefix: ${SANITIZER_LOG_PREFIX}"
log "LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-<unset>}"
log "App LD_LIBRARY_PATH=${APP_LD_LIBRARY_PATH:-<unset>}"
log "LD_PRELOAD=${LD_PRELOAD:-<unset>}"

if gamescope_session; then
    log "Detected gamescope session. XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-<unset>} XDG_SESSION_DESKTOP=${XDG_SESSION_DESKTOP:-<unset>} DESKTOP_SESSION=${DESKTOP_SESSION:-<unset>} GAMESCOPE_WAYLAND_DISPLAY=${GAMESCOPE_WAYLAND_DISPLAY:-<unset>}"
    GAMESCOPE_SESSION_DETECTED=1
else
    GAMESCOPE_SESSION_DETECTED=0
fi

if existing_pid="$(find_running_main_instance)"; then
    log "Existing AeroCore instance already running with pid ${existing_pid}; refusing second launch."
    exit 0
fi

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    log "No DISPLAY or WAYLAND_DISPLAY detected; launch AeroCore from a graphical session to reproduce browser crashes."
    exit 1
fi

declare -a candidates=()

append_xcb_candidates() {
    if [[ -z "${DISPLAY:-}" ]]; then
        return
    fi

    if [[ "${GPU_MODE}" == "x11-egl" || "${GPU_MODE}" == "auto" || "${GPU_MODE}" == "unsafe" ]]; then
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS}|xcb-gpu-${GPU_MODE}|${GPU_MODE}")
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS}|xcb-software-fallback|software")
    else
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS}|xcb-system-mouse-default|${GPU_MODE}")
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS} --disable-gpu --disable-software-rasterizer|xcb-system-mouse-fallback|${GPU_MODE}")
    fi
}

append_wayland_candidates() {
    if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
        return
    fi

    if [[ "${GPU_MODE}" == "auto" || "${GPU_MODE}" == "unsafe" ]]; then
        candidates+=("wayland|${BASE_CHROMIUM_FLAGS} --ozone-platform-hint=auto|wayland-gpu-${GPU_MODE}|${GPU_MODE}")
    fi
    candidates+=("wayland|${BASE_CHROMIUM_FLAGS} --ozone-platform=wayland --disable-gpu|wayland-software-fallback|software")
}

if [[ ${GAMESCOPE_SESSION_DETECTED} -eq 1 ]]; then
    append_wayland_candidates
    append_xcb_candidates
else
    append_xcb_candidates
    append_wayland_candidates
fi

if [[ -z "${DISPLAY:-}" ]]; then
    candidates+=("xcb|${BASE_CHROMIUM_FLAGS}|xcb-last-resort|software")
fi

for candidate in "${candidates[@]}"; do
    IFS='|' read -r platform flags note candidate_gpu_mode <<<"${candidate}"
    run_candidate "${platform}" "${flags}" "${note}" "${candidate_gpu_mode}" "$@"
    rc=$?
    if [[ ${rc} -eq 0 ]]; then
        exit 0
    fi

    if [[ ${rc} -eq 133 || ${rc} -eq 134 || ${rc} -eq 139 ]]; then
        log "Candidate ${platform} crashed immediately, trying next backend."
        continue
    fi
    exit ${rc}
done

log "All launcher candidates failed. Inspect ${LOG_FILE}."
exit 1
