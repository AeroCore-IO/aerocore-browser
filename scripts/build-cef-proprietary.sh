#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

CEF_BRANCH="${CEF_BRANCH:-7727}"
CEF_BUILD_DIR="${CEF_BUILD_DIR:-$ROOT_DIR/third_party/cef-source-build}"
CEF_INSTALL_ROOT="${CEF_INSTALL_ROOT:-$ROOT_DIR/third_party/cef}"
CEF_MIN_FREE_GB="${CEF_MIN_FREE_GB:-200}"
CEF_FORCE_CLEAN="${CEF_FORCE_CLEAN:-0}"
CEF_GCLIENT_JOBS="${CEF_GCLIENT_JOBS:-2}"
CEF_NINJA_JOBS="${CEF_NINJA_JOBS:-}"
CEF_INSTALL_SYSROOT="${CEF_INSTALL_SYSROOT:-x64}"
CEF_DISTRIB_FLAVOR="${CEF_DISTRIB_FLAVOR:-minimal}"
CEF_AUTOMATE_URL="${CEF_AUTOMATE_URL:-https://raw.githubusercontent.com/chromiumembedded/cef/master/tools/automate/automate-git.py}"
CEF_AUTOMATE_SCRIPT="${CEF_AUTOMATE_SCRIPT:-$CEF_BUILD_DIR/automate/automate-git.py}"
CEF_GN_DEFINES_DEFAULT="is_official_build=true use_sysroot=true symbol_level=1 is_cfi=false use_thin_lto=false chrome_pgo_phase=0 proprietary_codecs=true ffmpeg_branding=Chrome"
CEF_GN_DEFINES="${CEF_GN_DEFINES:-$CEF_GN_DEFINES_DEFAULT}"
CEF_ARCHIVE_FORMAT="${CEF_ARCHIVE_FORMAT:-tar.bz2}"
CEF_AUTOMATE_ARGS_DEFAULT=(
    --download-dir="$CEF_BUILD_DIR"
    --branch="$CEF_BRANCH"
    --minimal-distrib
    --client-distrib
    --no-chromium-history
    --no-debug-build
    --build-target=cefsimple
    --x64-build
)

usage() {
    cat <<EOF
Usage: $(basename "$0") [--check-only] [--download-script] [--build] [--install]

Build a Linux x64 CEF binary distribution with H.264/AAC enabled via:
  GN_DEFINES="$CEF_GN_DEFINES_DEFAULT"

Environment overrides:
  CEF_BRANCH           Chromium/CEF branch number (default: $CEF_BRANCH)
  CEF_BUILD_DIR        Chromium/CEF source checkout directory (default: $CEF_BUILD_DIR)
  CEF_INSTALL_ROOT     Extracted SDK install path used by AeroCore (default: $CEF_INSTALL_ROOT)
  CEF_MIN_FREE_GB      Required free disk space before download/build (default: $CEF_MIN_FREE_GB)
  CEF_FORCE_CLEAN      Set to 1 to discard existing checkout state before rebuild (default: $CEF_FORCE_CLEAN)
  CEF_GCLIENT_JOBS     Parallel jobs for recovery gclient sync (default: $CEF_GCLIENT_JOBS)
  CEF_NINJA_JOBS       Explicit autoninja -j value for Chromium/CEF build (default: auto)
  CEF_INSTALL_SYSROOT  Comma-separated Linux sysroots to install during gclient_hook (default: $CEF_INSTALL_SYSROOT)
  CEF_DISTRIB_FLAVOR   Archive to install: minimal, standard, or client (default: $CEF_DISTRIB_FLAVOR)
  CEF_GN_DEFINES       GN args for CEF/Chromium build
  CEF_AUTOMATE_ARGS    Extra/full automate-git.py args. If set, it replaces the default automate args.

Notes:
  - Official CEF docs recommend at least 16GB RAM and 200GB free disk for Linux builds.
EOF
}

free_gb_for_path() {
    local path="$1"
    mkdir -p "$path"
    df -Pk "$path" | awk 'NR == 2 { print int($4 / 1024 / 1024) }'
}

check_free_space() {
    local free_gb
    free_gb="$(free_gb_for_path "$CEF_BUILD_DIR")"
    echo ">>> Free disk near $CEF_BUILD_DIR: ${free_gb}GB"
    if (( free_gb < CEF_MIN_FREE_GB )); then
        cat >&2 <<EOF
Not enough disk space for a local CEF/Chromium build.
Required: ${CEF_MIN_FREE_GB}GB free
Current:  ${free_gb}GB free

Free more space or set CEF_BUILD_DIR to a larger external disk, for example:
  CEF_BUILD_DIR=/run/media/deck/build/cef-source-build make cef-build-proprietary
EOF
        exit 2
    fi
}

ensure_src_depot_tools_link() {
    local chromium_src_dir="$1"
    local depot_tools_dir="$2"
    local src_depot_tools_dir="$chromium_src_dir/third_party/depot_tools"

    if [[ -L "$src_depot_tools_dir" ]]; then
        return
    fi

    if [[ -d "$src_depot_tools_dir" ]]; then
        if [[ -z "$(find "$src_depot_tools_dir" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
            rmdir "$src_depot_tools_dir"
            ln -s "$depot_tools_dir" "$src_depot_tools_dir"
        fi
        return
    fi

    if [[ ! -e "$src_depot_tools_dir" ]]; then
        mkdir -p "$(dirname "$src_depot_tools_dir")"
        ln -s "$depot_tools_dir" "$src_depot_tools_dir"
    fi
}

chromium_checkout_missing_deps() {
    local chromium_src_dir="$1"

    [[ ! -f "$chromium_src_dir/v8/BUILD.gn" ]] && return 0
    [[ ! -f "$chromium_src_dir/third_party/angle/BUILD.gn" ]] && return 0
    [[ ! -f "$chromium_src_dir/third_party/angle/src/commit_id.py" ]] && return 0
    [[ ! -f "$chromium_src_dir/third_party/rust-toolchain/VERSION" ]] && return 0
    [[ ! -f "$chromium_src_dir/third_party/test_fonts/test_fonts/Ahem.ttf" ]] && return 0
    return 1
}

recover_existing_checkout() {
    local chromium_dir="$CEF_BUILD_DIR/chromium"
    local chromium_src_dir="$chromium_dir/src"
    local depot_tools_dir="$CEF_BUILD_DIR/depot_tools"
    local clang_bin="$chromium_src_dir/third_party/llvm-build/Release+Asserts/bin/clang"
    local rust_version_file="$chromium_src_dir/third_party/rust-toolchain/VERSION"
    local rust_update_script="$chromium_src_dir/tools/rust/update_rust.py"
    local node_dir="$chromium_src_dir/third_party/node"
    local node_bin="$node_dir/linux/node-linux-x64/bin/node"
    local node_update_script="$node_dir/update_node_binaries"
    local node_modules_dir="$node_dir/node_modules"
    local node_modules_sha1="$node_dir/node_modules.tar.gz.sha1"
    local node_modules_object=""
    local test_fonts_dir="$chromium_src_dir/third_party/test_fonts/test_fonts"
    local test_fonts_archive="$chromium_src_dir/third_party/test_fonts/test_fonts.tar.gz"
    local deps_file="$chromium_src_dir/DEPS"
    local test_fonts_object=""

    if [[ ! -f "$chromium_dir/.gclient" || ! -d "$chromium_src_dir" ]]; then
        return
    fi

    if [[ ! -x "$depot_tools_dir/gclient" ]]; then
        echo "Existing Chromium checkout needs recovery, but gclient is unavailable at $depot_tools_dir/gclient" >&2
        exit 6
    fi

    ensure_src_depot_tools_link "$chromium_src_dir" "$depot_tools_dir"

    if chromium_checkout_missing_deps "$chromium_src_dir"; then
        echo ">>> Existing Chromium checkout is missing dependency contents; running gclient sync --nohooks --ignore-dep-type=gcs --shallow -j $CEF_GCLIENT_JOBS"
        pushd "$chromium_dir" >/dev/null
        PATH="$depot_tools_dir:$PATH" "$depot_tools_dir/gclient" sync --nohooks --ignore-dep-type=gcs --shallow -j "$CEF_GCLIENT_JOBS"
        popd >/dev/null
        ensure_src_depot_tools_link "$chromium_src_dir" "$depot_tools_dir"
    fi

    if [[ ! -f "$rust_version_file" && -f "$rust_update_script" ]]; then
        echo ">>> Existing Chromium checkout is missing rust-toolchain; running tools/rust/update_rust.py"
        pushd "$chromium_src_dir" >/dev/null
        PATH="$depot_tools_dir:$PATH" python3 tools/rust/update_rust.py
        popd >/dev/null
    fi

    if [[ ! -x "$node_bin" && -x "$node_update_script" ]]; then
        echo ">>> Existing Chromium checkout is missing bundled Node.js; running third_party/node/update_node_binaries"
        pushd "$node_dir" >/dev/null
        ./update_node_binaries
        popd >/dev/null
    fi

    if [[ ( ! -d "$node_modules_dir" || -z "$(find "$node_modules_dir" -mindepth 1 -print -quit 2>/dev/null)" ) && -f "$node_modules_sha1" ]]; then
        echo ">>> Existing Chromium checkout is missing third_party/node/node_modules; restoring from GCS archive"
        pushd "$node_dir" >/dev/null
        rm -rf node_modules node_modules.tar.gz node_modules.tmp
        mkdir -p node_modules
        node_modules_object="$(tr -d '\r\n' < node_modules.tar.gz.sha1)"
        curl -L --fail --retry 3 --retry-delay 2 \
            --output node_modules.tar.gz \
            "https://storage.googleapis.com/chromium-nodejs/${node_modules_object}"
        tar xfz node_modules.tar.gz -C node_modules
        rm -f node_modules.tar.gz
        popd >/dev/null
    fi

    if [[ ! -f "$test_fonts_dir/Ahem.ttf" && -f "$deps_file" ]]; then
        echo ">>> Existing Chromium checkout is missing third_party/test_fonts/test_fonts; restoring from GCS archive"
        test_fonts_object="$(awk '
            /\x27src\/third_party\/test_fonts\/test_fonts\x27:/ { in_dep = 1; next }
            in_dep && /\x27object_name\x27:/ {
                if (match($0, /\x27object_name\x27: \x27[^\x27]+\x27/)) {
                    value = substr($0, RSTART, RLENGTH)
                    sub(/^.*\x27object_name\x27: \x27/, "", value)
                    sub(/\x27$/, "", value)
                    print value
                }
                exit
            }
            in_dep && /^  \x27src\// { exit }
        ' "$deps_file")"
        if [[ -z "$test_fonts_object" ]]; then
            echo "Failed to determine Chromium test_fonts archive object from $deps_file" >&2
            exit 10
        fi
        rm -rf "$test_fonts_dir" "$test_fonts_archive"
        mkdir -p "$test_fonts_dir"
        curl -L --fail --retry 3 --retry-delay 2 \
            --retry-all-errors \
            --output "$test_fonts_archive" \
            "https://storage.googleapis.com/chromium-fonts/${test_fonts_object}"
        tar xzf "$test_fonts_archive" -C "$test_fonts_dir"
        rm -f "$test_fonts_archive"
    fi

    if [[ ! -x "$node_bin" ]]; then
        echo "Bundled Node.js is still missing at $node_bin after recovery" >&2
        exit 8
    fi

    if [[ ! -d "$node_modules_dir" || -z "$(find "$node_modules_dir" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
        echo "Chromium node_modules are still missing at $node_modules_dir after recovery" >&2
        exit 9
    fi

    if [[ ! -f "$test_fonts_dir/Ahem.ttf" ]]; then
        echo "Chromium test fonts are still missing at $test_fonts_dir after recovery" >&2
        exit 10
    fi

    if [[ -x "$clang_bin" ]]; then
        return
    fi

    echo ">>> Existing Chromium checkout is missing bundled clang; running gclient runhooks"
    pushd "$chromium_dir" >/dev/null
    PATH="$depot_tools_dir:$PATH" "$depot_tools_dir/gclient" runhooks
    popd >/dev/null

    if [[ ! -x "$clang_bin" && -f "$chromium_src_dir/tools/clang/scripts/update.py" ]]; then
        echo ">>> Bundled clang is still missing after runhooks; running tools/clang/scripts/update.py with the configured proxy environment"
        pushd "$chromium_src_dir" >/dev/null
        PATH="$depot_tools_dir:$PATH" python3 tools/clang/scripts/update.py
        popd >/dev/null
    fi

    if [[ ! -x "$clang_bin" ]]; then
        echo "Bundled clang is still missing at $clang_bin after recovery hooks" >&2
        exit 7
    fi
}

should_force_build() {
    local chromium_src_dir="$CEF_BUILD_DIR/chromium/src"
    local distrib_dir="$chromium_src_dir/cef/binary_distrib"
    local out_dir="$chromium_src_dir/out"

    if find "$distrib_dir" -maxdepth 1 -type f -name 'cef_binary_*_linux64_minimal.tar.bz2' -print -quit 2>/dev/null | grep -q .; then
        return 1
    fi

    if [[ -d "$out_dir" ]]; then
        return 0
    fi

    return 1
}

download_automate_script() {
    mkdir -p "$(dirname "$CEF_AUTOMATE_SCRIPT")"
    if [[ -f "$CEF_AUTOMATE_SCRIPT" ]]; then
        echo ">>> Using existing $CEF_AUTOMATE_SCRIPT"
        return
    fi

    echo ">>> Downloading automate-git.py"
    curl -L --fail --retry 3 --retry-delay 2 \
        --output "$CEF_AUTOMATE_SCRIPT" \
        "$CEF_AUTOMATE_URL"
    chmod +x "$CEF_AUTOMATE_SCRIPT"
}

build_cef() {
    download_automate_script
    recover_existing_checkout
    export GN_DEFINES="$CEF_GN_DEFINES"
    export CEF_ARCHIVE_FORMAT
    export CEF_INSTALL_SYSROOT
    export CEF_NINJA_JOBS
    local automate_args=("${CEF_AUTOMATE_ARGS_DEFAULT[@]}")

    if [[ "$CEF_FORCE_CLEAN" == "1" ]]; then
        automate_args+=(--force-clean)
    fi

    if should_force_build; then
        automate_args+=(--force-build)
    fi

    echo ">>> GN_DEFINES=$GN_DEFINES"
    echo ">>> CEF_ARCHIVE_FORMAT=$CEF_ARCHIVE_FORMAT"
    echo ">>> CEF_FORCE_CLEAN=$CEF_FORCE_CLEAN"
    echo ">>> CEF_INSTALL_SYSROOT=$CEF_INSTALL_SYSROOT"
    if [[ -n "$CEF_NINJA_JOBS" ]]; then
        echo ">>> CEF_NINJA_JOBS=$CEF_NINJA_JOBS"
    fi
    if printf '%s\n' "${automate_args[@]}" | grep -qx -- '--force-build'; then
        echo ">>> Forcing build because no CEF binary distribution exists yet"
    fi

    if [[ -n "${CEF_AUTOMATE_ARGS:-}" ]]; then
        # shellcheck disable=SC2206
        local override_args=( ${CEF_AUTOMATE_ARGS} )
        python3 "$CEF_AUTOMATE_SCRIPT" "${override_args[@]}"
    else
        python3 "$CEF_AUTOMATE_SCRIPT" "${automate_args[@]}"
    fi
}

install_latest_distrib() {
    local distrib_dir archive_pattern archive_label archive tmpdir extracted
    distrib_dir="$CEF_BUILD_DIR/chromium/src/cef/binary_distrib"

    case "$CEF_DISTRIB_FLAVOR" in
        minimal)
            archive_pattern='cef_binary_*_linux64_minimal.tar.bz2'
            archive_label='linux64 minimal'
            ;;
        standard|full)
            archive_pattern='cef_binary_*_linux64.tar.bz2'
            archive_label='linux64 standard'
            ;;
        client)
            archive_pattern='cef_binary_*_linux64_client.tar.bz2'
            archive_label='linux64 client'
            ;;
        *)
            echo "Unsupported CEF_DISTRIB_FLAVOR: $CEF_DISTRIB_FLAVOR" >&2
            echo "Expected one of: minimal, standard, full, client" >&2
            exit 4
            ;;
    esac

    archive="$(find "$distrib_dir" -maxdepth 1 -type f -name "$archive_pattern" | sort -V | tail -n 1)"

    if [[ -z "$archive" ]]; then
        echo "No $archive_label CEF distribution found under $distrib_dir" >&2
        exit 5
    fi

    echo ">>> Installing $archive to $CEF_INSTALL_ROOT"
    tmpdir="$(mktemp -d)"
    tar -xf "$archive" -C "$tmpdir"
    extracted="$(find "$tmpdir" -mindepth 1 -maxdepth 1 -type d -name 'cef_binary_*_linux64*' | head -n 1)"
    if [[ -z "$extracted" ]]; then
        echo "Failed to extract $archive" >&2
        rm -rf "$tmpdir"
        exit 6
    fi

    rm -rf "$CEF_INSTALL_ROOT"
    mv "$extracted" "$CEF_INSTALL_ROOT"
    rm -rf "$tmpdir"
}

mode="build"
if [[ $# -eq 0 ]]; then
    mode="build"
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check-only)
            mode="check-only"
            ;;
        --download-script)
            mode="download-script"
            ;;
        --build)
            mode="build"
            ;;
        --install)
            mode="install"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

case "$mode" in
    check-only)
        check_free_space
        ;;
    download-script)
        check_free_space
        download_automate_script
        ;;
    build)
        check_free_space
        build_cef
        ;;
    install)
        install_latest_distrib
        ;;
esac
