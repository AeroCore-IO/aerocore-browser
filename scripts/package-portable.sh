#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist/aerocore-browser-portable}"
DIST_ARCHIVE="${DIST_ARCHIVE:-${DIST_DIR}.tar.gz}"
PACKAGE_ARCHIVE="${PACKAGE_ARCHIVE:-1}"
PACKAGE_VERSION="${PACKAGE_VERSION:-0.1.0}"
PACKAGE_VARIANT="${PACKAGE_VARIANT:-zh-CN}"
UPDATER_BINARY="${UPDATER_BINARY:-}"
APP_NAME="aerocore-browser"

qt_plugins_dir() {
    if [[ -n "${QT_PLUGIN_DIR:-}" ]]; then
        printf '%s\n' "$QT_PLUGIN_DIR"
        return 0
    fi

    if command -v qtpaths6 >/dev/null 2>&1; then
        qtpaths6 --query QT_INSTALL_PLUGINS
        return 0
    fi

    if command -v qtpaths >/dev/null 2>&1; then
        qtpaths --query QT_INSTALL_PLUGINS
        return 0
    fi

    return 1
}

qt_qml_dir() {
    if [[ -n "${QT_QML_DIR:-}" ]]; then
        printf '%s\n' "$QT_QML_DIR"
        return 0
    fi

    if command -v qtpaths6 >/dev/null 2>&1; then
        qtpaths6 --query QT_INSTALL_QML
        return 0
    fi

    if command -v qtpaths >/dev/null 2>&1; then
        qtpaths --query QT_INSTALL_QML
        return 0
    fi

    return 1
}

# Homebrew ships Qt as split formulae (qtbase, qtsvg, ...). The SVG image
# plugin may live outside qtpaths6's plugin tree, so copy it explicitly when
# the portable bundle is still missing libqsvg.so.
dist_svg_plugin_path() {
    local plugin

    for plugin in \
        "$DIST_DIR/plugins/imageformats/libqsvg.so" \
        "$DIST_DIR/plugins/imageformats/libqsvg.so.6"; do
        [[ -f "$plugin" ]] && {
            printf '%s\n' "$plugin"
            return 0
        }
    done
    return 1
}

locate_host_qsvg_plugins_dir() {
    local prefix path file

    if command -v brew >/dev/null 2>&1 && brew list --versions qtsvg >/dev/null 2>&1; then
        while IFS= read -r file; do
            [[ -f "$file" ]] || continue
            case "$file" in
                */imageformats/libqsvg.so*)
                    dirname "$file"
                    return 0
                    ;;
            esac
        done < <(brew list qtsvg 2>/dev/null || true)
    fi

    if command -v brew >/dev/null 2>&1; then
        prefix="$(brew --prefix 2>/dev/null || true)"
    fi
    [[ -n "${prefix:-}" ]] || return 1

    for path in \
        "$prefix/lib/qt6/plugins/imageformats" \
        "$prefix/plugins/imageformats" \
        "$prefix/opt/qtsvg/lib/qt6/plugins/imageformats" \
        "$prefix/opt/qtsvg/plugins/imageformats"; do
        if [[ -f "$path/libqsvg.so" || -f "$path/libqsvg.so.6" ]]; then
            printf '%s\n' "$path"
            return 0
        fi
    done

    while IFS= read -r file; do
        [[ -n "$file" ]] || continue
        printf '%s\n' "$(dirname "$file")"
        return 0
    done < <(find "$prefix" -path '*/plugins/imageformats/libqsvg.so*' -print 2>/dev/null | head -n 1)

    return 1
}

copy_supplemental_homebrew_qt_plugins() {
    local plugins_dir

    if dist_svg_plugin_path >/dev/null; then
        echo ">>> SVG image plugin already present: $(dist_svg_plugin_path)"
        return 0
    fi

    plugins_dir="$(locate_host_qsvg_plugins_dir || true)"
    if [[ -z "$plugins_dir" || ! -d "$plugins_dir" ]]; then
        echo ">>> Warning: libqsvg.so not found on build host (brew install qtsvg)." >&2
        return 0
    fi

    mkdir -p "$DIST_DIR/plugins/imageformats"
    cp -aL "$plugins_dir/." "$DIST_DIR/plugins/imageformats/"
    echo ">>> Copied SVG image plugins from ${plugins_dir}"
}

is_system_lib() {
    case "$1" in
        /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*|/usr/libexec/*)
            return 0
            ;;
    esac
    return 1
}

should_bundle_platform_system_lib() {
    case "$1" in
        libxcb-cursor.so*)
            # Qt 6.5+ xcb platform plugin requires libxcb-cursor, which is
            # often missing on SteamOS/Bazzite even though other X11 libs exist.
            return 0
            ;;
    esac
    return 1
}

should_skip_bundled_dep() {
    case "$1" in
        libX*.so*|libxkbcommon*.so*)
            # Keep X11 client libraries on the host; bundle the xcb stack with libqxcb.
            return 0
            ;;
    esac
    return 1
}

should_bundle_system_lib() {
    case "$1" in
        libQt6*.so*|libicu*.so*|libmd4c.so*)
            # Qt and ICU must be bundled even when the build host installs
            # them under /lib or /usr/lib. The AppImage cannot rely on host
            # Qt/ICU versions matching the build machine. Qt 6.8's GUI
            # library also has a runtime dependency on md4c.
            return 0
            ;;
    esac
    return 1
}

has_bundled_libxcb_cursor() {
    compgen -G "$DIST_DIR/lib/libxcb-cursor.so*" >/dev/null
}

locate_libxcb_cursor_dir() {
    local candidate dir

    if [[ -f "$DIST_DIR/plugins/platforms/libqxcb.so" ]]; then
        while IFS= read -r candidate; do
            [[ -n "$candidate" && -e "$candidate" ]] || continue
            dirname "$candidate"
            return 0
        done < <(ldd "$DIST_DIR/plugins/platforms/libqxcb.so" 2>/dev/null \
            | awk '/libxcb-cursor\.so/ {
                if ($3 ~ /^\//) { print $3; next }
                if ($1 ~ /^\//) print $1
            }')
    fi

    for dir in /usr/lib64 /usr/lib /lib64 /lib; do
        for candidate in "$dir"/libxcb-cursor.so*; do
            [[ -e "$candidate" ]] || continue
            printf '%s\n' "$dir"
            return 0
        done
    done

    while IFS= read -r candidate; do
        [[ -n "$candidate" && -e "$candidate" ]] || continue
        dirname "$candidate"
        return 0
    done < <(ldconfig -p 2>/dev/null | awk '/libxcb-cursor\.so/ { print $NF }' | sort -u)

    if command -v brew >/dev/null 2>&1; then
        local brew_prefix
        brew_prefix="$(brew --prefix 2>/dev/null || true)"
        for dir in \
            "${brew_prefix}/opt/xcb-util-cursor/lib" \
            "${brew_prefix}/lib"; do
            [[ -d "$dir" ]] || continue
            for candidate in "$dir"/libxcb-cursor.so*; do
                [[ -e "$candidate" ]] || continue
                printf '%s\n' "$dir"
                return 0
            done
        done
    fi

    return 1
}

bundle_libxcb_cursor() {
    local cursor_dir candidate cursor_lib

    if has_bundled_libxcb_cursor; then
        return 0
    fi

    cursor_dir="$(locate_libxcb_cursor_dir || true)"
    if [[ -z "$cursor_dir" ]]; then
        return 1
    fi

    for candidate in "$cursor_dir"/libxcb-cursor.so*; do
        [[ -e "$candidate" ]] || continue
        copy_dep "$candidate"
    done

    has_bundled_libxcb_cursor
}

mirror_libxcb_cursor_for_xcb_plugin() {
    local cursor_lib

    for cursor_lib in "$DIST_DIR/lib"/libxcb-cursor.so*; do
        [[ -f "$cursor_lib" ]] || continue
        cp -aL "$cursor_lib" "$DIST_DIR/plugins/platforms/$(basename "$cursor_lib")"
    done
}

verify_portable_xcb_plugin() {
    local qxcb="$DIST_DIR/plugins/platforms/libqxcb.so"
    local output missing cursor_line

    [[ -f "$qxcb" ]] || return 0

    output="$(env -u LD_LIBRARY_PATH ldd "$qxcb" 2>&1 || true)"
    missing="$(printf '%s\n' "$output" | grep 'not found' || true)"
    cursor_line="$(printf '%s\n' "$output" | grep 'libxcb-cursor' || true)"

    if [[ -n "$missing" || "$cursor_line" != *"$DIST_DIR"* ]]; then
        output="$(LD_LIBRARY_PATH="$DIST_DIR/lib:$DIST_DIR" ldd "$qxcb" 2>&1 || true)"
        missing="$(printf '%s\n' "$output" | grep 'not found' || true)"
        cursor_line="$(printf '%s\n' "$output" | grep 'libxcb-cursor' || true)"
    fi

    if [[ -n "$missing" ]]; then
        echo "Error: libqxcb.so still has unresolved dependencies with bundled lib path:" >&2
        printf '%s\n' "$output" >&2
        exit 4
    fi

    if [[ -z "$cursor_line" ]]; then
        echo "Error: libqxcb.so does not link libxcb-cursor." >&2
        exit 4
    fi

    if [[ "$cursor_line" != *"$DIST_DIR"* ]]; then
        echo "Error: libqxcb.so resolves libxcb-cursor outside the portable bundle:" >&2
        echo "  $cursor_line" >&2
        echo "Expected a path under $DIST_DIR/lib or plugins/platforms." >&2
        exit 4
    fi

    echo ">>> libqxcb.so resolves ${cursor_line}"
}

fix_libqxcb_rpath() {
    local qxcb="$DIST_DIR/plugins/platforms/libqxcb.so"

    [[ -f "$qxcb" ]] || return 0
    if ! command -v patchelf >/dev/null 2>&1; then
        echo "Error: patchelf is required to patch libqxcb.so rpath." >&2
        exit 4
    fi
    if ! patchelf --set-rpath '$ORIGIN:$ORIGIN/../../lib' "$qxcb" 2>/dev/null; then
        echo "Error: unable to set rpath on plugins/platforms/libqxcb.so" >&2
        exit 4
    fi
}

cef_runtime_resource_names() {
    printf '%s\n' \
        icudtl.dat \
        v8_context_snapshot.bin \
        snapshot_blob.bin \
        natives_blob.bin
}

find_libcef_dirs() {
    find "$DIST_DIR" -type f \( -name 'libcef.so' -o -name 'libcef.so.*' \) 2>/dev/null \
        | while IFS= read -r libcef; do
            dirname "$libcef"
        done | sort -u
}

sync_cef_resources_with_libcef() {
    local libcef_dir name src
    local -a libcef_dirs=()

    mapfile -t libcef_dirs < <(find_libcef_dirs)
    if [[ ${#libcef_dirs[@]} -eq 0 ]]; then
        echo "Warning: libcef.so not found in portable bundle." >&2
        return 0
    fi

    for libcef_dir in "${libcef_dirs[@]}"; do
        while IFS= read -r name; do
            [[ -n "$name" ]] || continue
            for src in "$DIST_DIR/$name" "$BUILD_DIR/$name"; do
                [[ -f "$src" ]] || continue
                if [[ ! -e "$libcef_dir/$name" ]]; then
                    cp -aL "$src" "$libcef_dir/$name"
                fi
                break
            done
        done < <(cef_runtime_resource_names)

        for src in "$DIST_DIR"/*.bin "$BUILD_DIR"/*.bin; do
            [[ -f "$src" ]] || continue
            name="$(basename "$src")"
            if [[ ! -e "$libcef_dir/$name" ]]; then
                cp -aL "$src" "$libcef_dir/$name"
            fi
        done

        for src in "$DIST_DIR"/*.pak "$BUILD_DIR"/*.pak; do
            [[ -f "$src" ]] || continue
            name="$(basename "$src")"
            if [[ ! -e "$libcef_dir/$name" ]]; then
                cp -aL "$src" "$libcef_dir/$name"
            fi
        done

        for pattern in libEGL.so libGLESv2.so libvk_swiftshader.so; do
            for src in "$DIST_DIR/${pattern}"* "$BUILD_DIR/${pattern}"*; do
                [[ -e "$src" ]] || continue
                name="$(basename "$src")"
                if [[ ! -e "$libcef_dir/$name" ]]; then
                    cp -aL "$src" "$libcef_dir/$name"
                fi
            done
        done
    done
}

verify_cef_runtime_resources() {
    local libcef_dir pak_name
    local -a libcef_dirs=()
    local -a required_paks=(resources.pak chrome_100_percent.pak)

    mapfile -t libcef_dirs < <(find_libcef_dirs)
    for libcef_dir in "${libcef_dirs[@]}"; do
        if [[ ! -f "$libcef_dir/icudtl.dat" ]]; then
            echo "Error: icudtl.dat must be next to libcef.so in $libcef_dir" >&2
            echo "CEF loads ICU data from the libcef.so directory, not the executable directory." >&2
            exit 5
        fi
        for pak_name in "${required_paks[@]}"; do
            if [[ ! -f "$libcef_dir/$pak_name" ]]; then
                echo "Error: $pak_name must be next to libcef.so in $libcef_dir" >&2
                exit 5
            fi
        done
        if [[ ! -e "$libcef_dir/libGLESv2.so" ]]; then
            echo "Warning: libGLESv2.so missing beside libcef.so in $libcef_dir" >&2
        fi
    done

    if [[ ${#libcef_dirs[@]} -gt 0 ]]; then
        echo ">>> CEF runtime resources synced beside libcef.so in: ${libcef_dirs[*]}"
    fi
}

copy_build_payload() {
    local entry name

    mkdir -p "$DIST_DIR"

    while IFS= read -r entry; do
        name="${entry##*/}"
        case "$name" in
            CMakeCache.txt|cmake_install.cmake|Makefile|CMakeFiles|aerocore-browser_autogen|updater|Testing|CTestTestfile.cmake|tst_*)
                # Test executables and CTest metadata are not runtime payload.
                continue
                ;;
        esac
        cp -a "$entry" "$DIST_DIR/$name"
    done < <(find "$BUILD_DIR" -maxdepth 1 -mindepth 1 | sort)
}

copy_updater() {
    [[ -n "$UPDATER_BINARY" ]] || return 0
    [[ -x "$UPDATER_BINARY" ]] || {
        echo "Missing updater binary: $UPDATER_BINARY" >&2
        exit 1
    }
    mkdir -p "$DIST_DIR/updater"
    install -m 0755 "$UPDATER_BINARY" "$DIST_DIR/updater/aerocore-browser-updater"
}

write_release_manifest() {
    cat >"$DIST_DIR/manifest.yaml" <<EOF
version: "$PACKAGE_VERSION"
platform: "linux-x86_64"
variant: "$PACKAGE_VARIANT"
generated_at: "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
EOF
}

install_distribution_profile() {
    case "$PACKAGE_VARIANT" in
        zh-CN|international) ;;
        *) echo "Unsupported PACKAGE_VARIANT: $PACKAGE_VARIANT" >&2; exit 2 ;;
    esac
    install -d "$DIST_DIR/config"
    install -m 0644 "$ROOT_DIR/packaging/$PACKAGE_VARIANT/distribution.json" \
        "$DIST_DIR/config/distribution.json"
    install -m 0644 "$ROOT_DIR/packaging/$PACKAGE_VARIANT/release-source.json" \
        "$DIST_DIR/config/release-source.json"
    if [[ "$PACKAGE_VARIANT" == "zh-CN" ]]; then
        install -d "$DIST_DIR/resources"
        install -m 0644 "$ROOT_DIR/src/content/res/aerocore-wechat-qr.png" \
            "$DIST_DIR/resources/aerocore-wechat-qr.png"
    else
        rm -f "$DIST_DIR/resources/aerocore-wechat-qr.png"
    fi
}

copy_release_assets() {
    # These files are normally added only to the AppImage wrapper. Keep the
    # installed runtime self-contained so a runtime update does not remove
    # the launcher's icon or its installed AppRun.
    if [[ -f "$ROOT_DIR/appimage/InstalledAppRun" ]]; then
        install -m 0755 "$ROOT_DIR/appimage/InstalledAppRun" "$DIST_DIR/AppRun"
    fi
    if [[ -f "$ROOT_DIR/appimage/io.aerocore.Browser.svg" ]]; then
        install -m 0644 "$ROOT_DIR/appimage/io.aerocore.Browser.svg" "$DIST_DIR/io.aerocore.Browser.svg"
    fi
    install -d "$DIST_DIR/usr/libexec"
    install -m 0755 "$ROOT_DIR/scripts/ensure-widevine-cdm.sh" \
        "$DIST_DIR/usr/libexec/aerocore-browser-ensure-widevine-cdm"
}

copy_qt_plugins() {
    local plugin

    for plugin in \
        platforms/libqxcb.so \
        imageformats/libqsvg.so \
        imageformats/libqpng.so \
        imageformats/libqjpeg.so \
        imageformats/libqico.so \
        sqldrivers/libqsqlite.so \
        styles/libqfusionstyle.so \
        tls/libqopensslbackend.so; do

        if [[ -e "$qt_plugin_dir/$plugin" ]]; then
            mkdir -p "$DIST_DIR/plugins/$(dirname "$plugin")"
            cp -aL "$qt_plugin_dir/$plugin" \
                "$DIST_DIR/plugins/$plugin"
        fi
    done

    if [[ -d "$qt_plugin_dir/xcbglintegrations" ]]; then
        mkdir -p "$DIST_DIR/plugins/xcbglintegrations"
        cp -aL "$qt_plugin_dir/xcbglintegrations/." \
            "$DIST_DIR/plugins/xcbglintegrations/"
    fi
}

verify_sqlite_driver() {
    local plugin

    plugin="$DIST_DIR/plugins/sqldrivers/libqsqlite.so"
    [[ -f "$plugin" ]] || plugin="$DIST_DIR/plugins/sqldrivers/libqsqlite.so.6"
    [[ -f "$plugin" ]] || {
        echo "Error: Qt SQLite SQL driver missing from portable bundle." >&2
        echo "Install libqt6sql6-sqlite on Debian/Ubuntu or ensure the Qt SQL driver is available on the build host." >&2
        exit 6
    }

    echo ">>> Qt SQLite SQL driver: $(readlink -f "$plugin")"
}

copy_qt_qml_modules() {
    local module

    for module in \
        QtQml \
        QtQml/Models \
        QtQuick \
        QtQuick/Controls \
        QtQuick/Layouts \
        QtQuick/Templates \
        QtQuick/Window \
        QtQuick/Effects; do

        if [[ -d "$qt_qml_dir/$module" ]]; then
            mkdir -p "$DIST_DIR/qml/$module"
            cp -aL "$qt_qml_dir/$module/." "$DIST_DIR/qml/$module/"
        fi
    done
}

ensure_config_payload() {
    if [[ -f "$DIST_DIR/config/ui/opbarsetting.json" ]]; then
        echo ">>> Runtime config present: $DIST_DIR/config"
        return 0
    fi

    if [[ -d "$ROOT_DIR/config" ]]; then
        cp -a "$ROOT_DIR/config" "$DIST_DIR/config"
        echo ">>> Copied runtime config from $ROOT_DIR/config"
        return 0
    fi

    echo "Error: config/ui runtime files missing from portable bundle." >&2
    echo "Rebuild with 'make build' so POST_BUILD copies config into $BUILD_DIR." >&2
    exit 6
}

report_widevine_payload() {
    if [[ -f "$DIST_DIR/WidevineCdm/manifest.json" \
        && -f "$DIST_DIR/WidevineCdm/_platform_specific/linux_x64/libwidevinecdm.so" ]]; then
        echo ">>> Widevine CDM present: $DIST_DIR/WidevineCdm"
    else
        echo "Warning: Widevine CDM missing from portable bundle; DRM playback will be unavailable." >&2
    fi
}

ensure_widevine_payload() {
    local cdm_source="$ROOT_DIR/third_party/widevine/WidevineCdm"
    if [[ -f "$DIST_DIR/WidevineCdm/manifest.json" ]]; then
        return 0
    fi
    if [[ -f "$cdm_source/manifest.json" ]]; then
        mkdir -p "$DIST_DIR/WidevineCdm"
        cp -a "$cdm_source/." "$DIST_DIR/WidevineCdm/"
    fi
}

verify_config_runtime() {
    local required=(
        config/ui/opbarsetting.json
        config/ui/key_help.json
        config/ui/image/xbox/op-keya.svg
    )
    local rel

    for rel in "${required[@]}"; do
        [[ -f "$DIST_DIR/$rel" ]] || {
            echo "Error: missing portable runtime config file: $rel" >&2
            exit 6
        }
    done

    echo ">>> Runtime config files verified under $DIST_DIR/config"
}

declare -A seen_deps=()

copy_dep_shallow() {
    local dep_path dep_name

    dep_path="$1"
    dep_name="$(basename "$dep_path")"

    mkdir -p "$DIST_DIR/lib"
    if [[ ! -e "$DIST_DIR/lib/$dep_name" ]]; then
        cp -aL "$dep_path" "$DIST_DIR/lib/$dep_name"
    fi
}

copy_dep() {
    local dep_path dep_name

    dep_path="$1"
    dep_name="$(basename "$dep_path")"

    if should_skip_bundled_dep "$dep_name"; then
        return 0
    fi

    mkdir -p "$DIST_DIR/lib"
    if [[ ! -e "$DIST_DIR/lib/$dep_name" ]]; then
        cp -aL "$dep_path" "$DIST_DIR/lib/$dep_name"
    fi

    collect_deps "$dep_path"
}

collect_deps() {
    local target dep

    target="$1"
    if [[ ! -e "$target" || -n "${seen_deps[$target]:-}" ]]; then
        return 0
    fi
    seen_deps["$target"]=1

    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        if is_system_lib "$dep"; then
            if ! should_bundle_system_lib "$(basename "$dep")"; then
                continue
            fi
        fi
        case "$dep" in
            "$DIST_DIR"/*)
                continue
                ;;
        esac
        if should_skip_bundled_dep "$(basename "$dep")"; then
            continue
        fi
        copy_dep "$dep"
    done < <(ldd "$target" 2>/dev/null | awk '/=> \/[^ ]+/ { print $3 } /^\/[^ ]+ \(/ { print $1 }')
}

bundle_non_system_deps() {
    while IFS= read -r target; do
        collect_deps "$target"
    done < <(
        {
            find "$DIST_DIR" -maxdepth 1 -type f \( -name "$APP_NAME" -o -name '*.so' -o -name '*.so.*' \)
            find "$DIST_DIR/plugins" -type f \( -name '*.so' -o -name '*.so.*' \)
            find "$DIST_DIR/qml" -type f \( -name '*.so' -o -name '*.so.*' \)
        } | sort -u
    )
}

prune_bundled_x11_libs() {
    local lib_path base removed=0

    shopt -s nullglob
    for lib_path in "$DIST_DIR/lib"/libX*.so* "$DIST_DIR/lib"/libxkbcommon*.so*; do
        base="$(basename "$lib_path")"
        if should_skip_bundled_dep "$base"; then
            rm -f "$lib_path"
            removed=$((removed + 1))
        fi
    done
    shopt -u nullglob

    if [[ ${removed} -gt 0 ]]; then
        echo ">>> Removed ${removed} bundled libX/libxkbcommon libraries from lib/ (kept libxcb stack for libqxcb.so)"
    fi
}

bundle_libqxcb_runtime_deps() {
    local qxcb="$DIST_DIR/plugins/platforms/libqxcb.so"
    local dep dep_name

    [[ -f "$qxcb" ]] || return 0

    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        dep_name="$(basename "$dep")"
        case "$dep_name" in
            libxcb*.so*)
                if is_system_lib "$dep"; then
                    copy_dep_shallow "$dep"
                else
                    copy_dep "$dep"
                fi
                ;;
        esac
    done < <(ldd "$qxcb" 2>/dev/null | awk '/=> \/[^ ]+/ { print $3 } /^\/[^ ]+ \(/ { print $1 }')
}

bundle_qt_platform_system_deps() {
    local platform_dir="$DIST_DIR/plugins/platforms"
    local target dep dep_name

    [[ -d "$platform_dir" ]] || return 0

    while IFS= read -r target; do
        while IFS= read -r dep; do
            [[ -n "$dep" ]] || continue
            dep_name="$(basename "$dep")"
            if is_system_lib "$dep" && should_bundle_platform_system_lib "$dep_name"; then
                copy_dep_shallow "$dep"
            fi
        done < <(ldd "$target" 2>/dev/null | awk '/=> \/[^ ]+/ { print $3 } /^\/[^ ]+ \(/ { print $1 }')
    done < <(find "$platform_dir" -maxdepth 1 -type f -name 'libq*.so' | sort -u)

    if ! bundle_libxcb_cursor; then
        echo "Error: libxcb-cursor was not found on the build machine." >&2
        echo "Install one of these packages and rerun make package-portable:" >&2
        echo "  Fedora/Bazzite: sudo dnf install xcb-util-cursor" >&2
        echo "  Debian/Ubuntu:  sudo apt install libxcb-cursor0" >&2
        echo "  Homebrew Linux: brew install xcb-util-cursor" >&2
        exit 3
    fi

    echo ">>> Bundled libxcb-cursor: $(ls -1 "$DIST_DIR/lib"/libxcb-cursor.so* 2>/dev/null | tr '\n' ' ')"
    mirror_libxcb_cursor_for_xcb_plugin
}

plugin_rpath_for() {
    relative_rpath_to_lib "$DIST_DIR/plugins" "$1"
}

relative_rpath_to_lib() {
    local root_dir="$1"
    local file_path="$2"
    local rel="${file_path#"$root_dir/"}"
    local slashes lib_rpath='$ORIGIN'

    slashes="$(printf '%s' "$rel" | tr -cd '/' | wc -c | tr -d ' ')"
    for ((i = 0; i <= slashes; i++)); do
        lib_rpath+='/..'
    done
    lib_rpath+='/lib'
    printf '%s\n' "\$ORIGIN:${lib_rpath}"
}

bundle_qml_plugin_deps() {
    local target dep dep_name

    while IFS= read -r target; do
        collect_deps "$target"
        while IFS= read -r dep; do
            [[ -n "$dep" ]] || continue
            dep_name="$(basename "$dep")"
            case "$dep_name" in
                libQt6*.so*)
                    copy_dep "$dep"
                    ;;
            esac
        done < <(ldd "$target" 2>/dev/null | awk '/=> \/[^ ]+/ { print $3 } /^\/[^ ]+ \(/ { print $1 }')
    done < <(find "$DIST_DIR/qml" -type f \( -name '*.so' -o -name '*.so.*' \) | sort -u)

    if [[ ! -e "$DIST_DIR/lib/libQt6QuickTemplates2.so.6" ]]; then
        echo "Error: libQt6QuickTemplates2.so.6 was not bundled for QML Controls." >&2
        exit 6
    fi
}

fix_qml_plugin_rpaths() {
    local target real rpath

    if ! command -v patchelf >/dev/null 2>&1; then
        echo "Error: patchelf is required to patch QML plugin rpaths." >&2
        exit 6
    fi

    while IFS= read -r target; do
        [[ -e "$target" ]] || continue
        real="$(readlink -f "$target")"
        [[ -f "$real" ]] || continue

        rpath="$(relative_rpath_to_lib "$DIST_DIR/qml" "$real")"
        if ! patchelf --set-rpath "$rpath" "$real" 2>/dev/null; then
            echo "Error: unable to set rpath on ${target#"$DIST_DIR/"}" >&2
            exit 6
        fi
    done < <(find "$DIST_DIR/qml" \( -type f -o -type l \) \( -name '*.so' -o -name '*.so.*' \) | sort -u)
}

fix_svg_image_plugin_rpath() {
    local plugin real

    for plugin in \
        "$DIST_DIR/plugins/imageformats/libqsvg.so" \
        "$DIST_DIR/plugins/imageformats/libqsvg.so.6"; do
        [[ -e "$plugin" ]] || continue
        real="$(readlink -f "$plugin")"
        [[ -f "$real" ]] || continue
        if ! command -v patchelf >/dev/null 2>&1; then
            echo "Warning: patchelf not found; skipping libqsvg.so rpath fix." >&2
            return 0
        fi
        if ! patchelf --set-rpath '$ORIGIN:$ORIGIN/../../lib' "$real" 2>/dev/null; then
            echo "Error: unable to set rpath on ${plugin#"$DIST_DIR/"}" >&2
            exit 6
        fi
        echo ">>> libqsvg.so rpath: $(patchelf --print-rpath "$real" 2>/dev/null || echo '<unset>')"
        return 0
    done
}

ensure_svg_runtime_libs() {
    local plugin real

    plugin="$(dist_svg_plugin_path || true)"
    [[ -n "$plugin" ]] || return 0
    real="$(readlink -f "$plugin")"
    collect_deps "$real"

    if ! compgen -G "$DIST_DIR/lib/libQt6Svg.so*" >/dev/null; then
        echo "Error: libQt6Svg.so* missing from portable bundle (required by libqsvg.so)." >&2
        exit 6
    fi

    echo ">>> SVG runtime library bundled: $(compgen -G "$DIST_DIR/lib/libQt6Svg.so*" | head -n1)"
}

verify_svg_image_plugin() {
    local plugin="$DIST_DIR/plugins/imageformats/libqsvg.so"
    local real_plugin output missing qt_line

    [[ -f "$plugin" ]] || plugin="$DIST_DIR/plugins/imageformats/libqsvg.so.6"
    [[ -f "$plugin" ]] || {
        echo "Error: Qt SVG image plugin missing from portable bundle." >&2
        echo "Install qtsvg on the build machine, then rerun 'make package-portable'." >&2
        echo "On Homebrew Linux, libqsvg.so is usually under \$(brew --prefix)/lib/qt6/plugins/imageformats/." >&2
        exit 6
    }

    if ! compgen -G "$DIST_DIR/lib/libQt6Svg.so*" >/dev/null; then
        echo "Error: libQt6Svg.so* missing from portable bundle." >&2
        exit 6
    fi

    real_plugin="$(readlink -f "$plugin")"
    output="$(LD_LIBRARY_PATH="$DIST_DIR/lib:$DIST_DIR" ldd "$real_plugin" 2>&1 || true)"
    missing="$(printf '%s\n' "$output" | grep 'not found' || true)"
    qt_line="$(printf '%s\n' "$output" | grep 'libQt6Svg' || true)"
    if [[ -n "$missing" ]]; then
        echo "Error: libqsvg.so has unresolved dependencies with bundled lib path:" >&2
        printf '%s\n' "$output" >&2
        exit 6
    fi

    if [[ -n "$qt_line" && "$qt_line" != *"$DIST_DIR"* ]]; then
        echo "Error: libqsvg.so resolves libQt6Svg outside the portable bundle:" >&2
        echo "  $qt_line" >&2
        exit 6
    fi

    echo ">>> SVG image plugin: ${real_plugin}"
    if [[ -n "$qt_line" ]]; then
        echo ">>> libqsvg.so resolves ${qt_line}"
    fi
}

verify_qml_controls_plugin() {
    local plugin="$DIST_DIR/qml/QtQuick/Controls/libqtquickcontrols2plugin.so"
    local real_plugin output qt_line missing runpath

    [[ -f "$plugin" ]] || return 0
    real_plugin="$(readlink -f "$plugin")"

    output="$(env -u LD_LIBRARY_PATH ldd "$real_plugin" 2>&1 || true)"
    qt_line="$(printf '%s\n' "$output" | grep 'libQt6QuickTemplates2' || true)"
    missing="$(printf '%s\n' "$output" | grep 'not found' || true)"
    runpath="$(patchelf --print-rpath "$real_plugin" 2>/dev/null || true)"

    if [[ -n "$missing" ]]; then
        echo "Error: libqtquickcontrols2plugin.so has unresolved dependencies:" >&2
        printf '%s\n' "$output" >&2
        exit 6
    fi

    if [[ -n "$qt_line" && "$qt_line" != *"$DIST_DIR"* ]]; then
        echo "Error: QML Controls plugin resolves Qt outside the portable bundle:" >&2
        echo "  $qt_line" >&2
        echo "  patchelf rpath: ${runpath:-<unset>}" >&2
        exit 6
    fi

    if [[ -n "$qt_line" ]]; then
        echo ">>> libqtquickcontrols2plugin.so resolves ${qt_line}"
    fi
}

apply_portable_rpaths() {
    local target rpath real
    declare -A patched_plugins=()

    if ! command -v patchelf >/dev/null 2>&1; then
        echo "Warning: patchelf not found; skipping rpath fixups." >&2
        return 0
    fi

    is_patchelf_target() {
        local file_path="$1"
        [[ -f "$file_path" && ! -L "$file_path" ]] || return 1
        # `file` is localized on some build hosts, so do not match its
        # human-readable output when deciding whether a file is ELF.
        readelf -h "$file_path" >/dev/null 2>&1
    }

    set_rpath() {
        local file_path="$1" new_rpath="$2" label="$3"

        is_patchelf_target "$file_path" || return 0
        patchelf --set-rpath "$new_rpath" "$file_path" 2>/dev/null \
            || echo "Warning: unable to set rpath on ${label}" >&2
    }

    if [[ -f "$DIST_DIR/$APP_NAME" ]]; then
        set_rpath "$DIST_DIR/$APP_NAME" '$ORIGIN/lib' "$APP_NAME"
    fi

    while IFS= read -r target; do
        set_rpath "$target" '$ORIGIN' "${target#"$DIST_DIR/"}"
    done < <(find "$DIST_DIR/lib" -maxdepth 1 -type f \( -name '*.so' -o -name '*.so.*' \) | sort -u)

    while IFS= read -r target; do
        [[ -e "$target" ]] || continue
        real="$(readlink -f "$target")"
        [[ -f "$real" ]] || continue
        [[ -n "${patched_plugins[$real]:-}" ]] && continue
        patched_plugins[$real]=1
        rpath="$(plugin_rpath_for "$real")"
        set_rpath "$real" "$rpath" "${target#"$DIST_DIR/"}"
    done < <(find "$DIST_DIR/plugins" -type f \( -name '*.so' -o -name '*.so.*' \) | sort -u)
}

write_launcher() {
    cat >"$DIST_DIR/run.sh" <<'EOF'
#!/usr/bin/env bash
set -u

APP_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_PATH="${APP_DIR}/aerocore-browser"
CEF_PROFILE_DIR="${XDG_DATA_HOME:-${HOME}/.local/share}/AeroCore/AeroCore Browser/cef-profile"
export AEROCORE_CEF_PROFILE_DIR="${AEROCORE_CEF_PROFILE_DIR:-${CEF_PROFILE_DIR}}"
WIDEVINE_OUT_DIR="${APP_DIR}/WidevineCdm"
if [[ -n "${APPIMAGE:-}" || ! -w "${APP_DIR}" ]]; then
    WIDEVINE_OUT_DIR="${AEROCORE_CEF_PROFILE_DIR}/WidevineCdm"
fi
export AEROCORE_WIDEVINE_DIR="${WIDEVINE_OUT_DIR}"
STATE_LOG_DIR="${XDG_STATE_HOME:-${HOME}/.local/state}/aerocore-browser"
STATE_LOG_FILE="${STATE_LOG_DIR}/launcher.log"

mkdir -p "${STATE_LOG_DIR}"

# AppImage mounts APP_DIR read-only. Keep the historical bundle-local log
# when possible, but always fall back to the writable user state directory.
if [[ -n "${AEROCORE_PORTABLE_LOG_FILE:-}" ]]; then
    PORTABLE_LOG_FILE="${AEROCORE_PORTABLE_LOG_FILE}"
elif [[ -z "${APPIMAGE:-}" && -w "${APP_DIR}" ]]; then
    PORTABLE_LOG_FILE="${APP_DIR}/launcher.log"
else
    PORTABLE_LOG_FILE="${STATE_LOG_FILE}"
fi

timestamp() {
    date '+%F %T'
}

log() {
    local line
    line="$(printf '[%s] %s' "$(timestamp)" "$*")"
    printf '%s\n' "${line}" >>"${PORTABLE_LOG_FILE}"
    if [[ "${PORTABLE_LOG_FILE}" != "${STATE_LOG_FILE}" ]]; then
        printf '%s\n' "${line}" >>"${STATE_LOG_FILE}"
    fi
    printf '%s\n' "${line}" >&2
}

log "Portable launcher invoked from ${APP_DIR} (argv: $*)"
log "Portable log: ${PORTABLE_LOG_FILE}"
log "State log: ${STATE_LOG_FILE}"

WIDEVINE_HELPER="${APP_DIR}/usr/libexec/aerocore-browser-ensure-widevine-cdm"
if [[ -x "${WIDEVINE_HELPER}" ]]; then
    export AEROCORE_WIDEVINE_HELPER="${WIDEVINE_HELPER}"
fi

if [[ ! -x "${APP_PATH}" ]]; then
    log "Missing executable: ${APP_PATH}"
    exit 1
fi

if ! compgen -G "${APP_DIR}/lib/libxcb-cursor.so*" >/dev/null; then
    log "Bundled libxcb-cursor is missing from ${APP_DIR}/lib"
    log "Rebuild the portable bundle on a machine with xcb-util-cursor / libxcb-cursor0 installed."
    exit 1
fi

# Qt requires a UTF-8 locale.  Steam/GameScope can provide LANG=... while
# leaving LC_ALL=C, and LC_ALL takes precedence over LANG; normalize both
# variables so the application does not emit Qt's "Detected locale C"
# warning in that environment.
if [[ -z "${LANG:-}" || "${LANG}" == "C" || "${LANG}" == "POSIX" ]]; then
    export LANG="C.UTF-8"
fi
if [[ -z "${LC_ALL:-}" || "${LC_ALL}" == "C" || "${LC_ALL}" == "POSIX" ]]; then
    export LC_ALL="${LANG}"
fi
export DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SYSTEM_BUS_ADDRESS:-unix:path=/run/dbus/system_bus_socket}"

if [[ "${AEROCORE_KEEP_LD_PRELOAD:-}" != "1" && -n "${LD_PRELOAD:-}" ]]; then
    log "Clearing LD_PRELOAD (Steam game overlay breaks Qt xcb plugin loading)."
    unset LD_PRELOAD
fi

export LD_LIBRARY_PATH="${APP_DIR}/lib:${APP_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

LIBXCB_CURSOR="$(compgen -G "${APP_DIR}/plugins/platforms/libxcb-cursor.so*" | head -n1 || true)"
if [[ -z "${LIBXCB_CURSOR}" ]]; then
    LIBXCB_CURSOR="$(compgen -G "${APP_DIR}/lib/libxcb-cursor.so*" | head -n1 || true)"
fi
if [[ -n "${LIBXCB_CURSOR}" ]]; then
    export LD_PRELOAD="${LIBXCB_CURSOR}${LD_PRELOAD:+:${LD_PRELOAD}}"
    log "Preloading libxcb-cursor from ${LIBXCB_CURSOR}"
fi

export QT_PLUGIN_PATH="${APP_DIR}/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="${APP_DIR}/plugins/platforms"
export QML2_IMPORT_PATH="${APP_DIR}/qml"

log_xcb_plugin_diagnostics() {
    local qxcb="${APP_DIR}/plugins/platforms/libqxcb.so"
    local output missing cursor_line

    [[ -f "${qxcb}" ]] || return 0
    output="$(LD_LIBRARY_PATH="${APP_DIR}/lib:${APP_DIR}" ldd "${qxcb}" 2>&1 || true)"
    missing="$(printf '%s\n' "${output}" | grep 'not found' || true)"
    cursor_line="$(printf '%s\n' "${output}" | grep 'libxcb-cursor' || true)"
    if [[ -n "${cursor_line}" ]]; then
        log "libqxcb.so cursor dependency: ${cursor_line}"
    fi
    if [[ -n "${missing}" ]]; then
        log "libqxcb.so unresolved dependencies with bundled lib path:"
        while IFS= read -r line; do
            [[ -n "${line}" ]] || continue
            log "  ${line}"
        done <<<"${missing}"
    fi
}

if [[ -n "${AEROCORE_DEBUG_PLUGINS:-}" ]]; then
    export QT_DEBUG_PLUGINS=1
    export QT_LOGGING_RULES="${QT_LOGGING_RULES:-qt.qpa.*=true}"
fi

log_xcb_plugin_diagnostics

log_svg_plugin_diagnostics() {
    local plugin real output missing qt_line

    for plugin in \
        "${APP_DIR}/plugins/imageformats/libqsvg.so" \
        "${APP_DIR}/plugins/imageformats/libqsvg.so.6"; do
        [[ -e "${plugin}" ]] || continue
        real="$(readlink -f "${plugin}")"
        output="$(LD_LIBRARY_PATH="${APP_DIR}/lib:${APP_DIR}" ldd "${real}" 2>&1 || true)"
        missing="$(printf '%s\n' "${output}" | grep 'not found' || true)"
        qt_line="$(printf '%s\n' "${output}" | grep 'libQt6Svg' || true)"
        log "SVG image plugin: ${plugin}"
        if [[ -n "${qt_line}" ]]; then
            log "libqsvg.so resolves ${qt_line}"
        fi
        if [[ -n "${missing}" ]]; then
            log "libqsvg.so unresolved dependencies with bundled lib path:"
            while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done <<<"${missing}"
        fi
        return 0
    done

    log "SVG image plugin missing from ${APP_DIR}/plugins/imageformats/"
}

log_svg_plugin_diagnostics

log_binary_diagnostics() {
    local output missing

    output="$(LD_LIBRARY_PATH="${APP_DIR}/lib:${APP_DIR}" ldd "${APP_PATH}" 2>&1 || true)"
    missing="$(printf '%s\n' "${output}" | grep 'not found' || true)"
    if [[ -n "${missing}" ]]; then
        log "aerocore-browser missing shared libraries:"
        while IFS= read -r line; do
            [[ -n "${line}" ]] || continue
            log "  ${line}"
        done <<<"${missing}"
    fi
}

log_binary_diagnostics

resolve_libcef_dir() {
    local line path

    line="$(LD_LIBRARY_PATH="${APP_DIR}/lib:${APP_DIR}" ldd "${APP_PATH}" 2>/dev/null | awk '/libcef\.so/ { print; exit }' || true)"
    [[ -n "${line}" ]] || return 1

    if [[ "${line}" == *" => "* ]]; then
        path="${line#*=> }"
        path="${path%% *}"
    else
        path="${line%% *}"
    fi

    [[ "${path}" == /* && -e "${path}" ]] || return 1
    dirname "${path}"
}

libcef_dir="$(resolve_libcef_dir || true)"
if [[ -n "${libcef_dir}" && ! -f "${libcef_dir}/icudtl.dat" ]]; then
    log "Missing icudtl.dat next to libcef.so in ${libcef_dir}"
    log "Rebuild the portable bundle; CEF requires ICU data beside libcef.so."
    exit 1
fi

BASE_CHROMIUM_FLAGS="${AEROCORE_CHROMIUM_FLAGS:-}"
GPU_MODE="${AEROCORE_GPU_MODE:-auto}"

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

clear_stale_cef_singleton() {
    local profile_dir="${XDG_DATA_HOME:-${HOME}/.local/share}/AeroCore/AeroCore Browser"
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

run_candidate() {
    local platform="$1"
    local extra_flags="$2"
    local note="$3"
    local candidate_gpu_mode="$4"
    local start_ts end_ts elapsed rc
    shift 4

    local -a flag_args=()
    local suppress_xcb_keyboard="${AEROCORE_SUPPRESS_XCB_KEYBOARD:-}"
    if [[ -n "${extra_flags}" ]]; then
        read -r -a flag_args <<<"${extra_flags}"
    fi

    if [[ "${platform}" == "xcb" && -z "${suppress_xcb_keyboard}" ]]; then
        suppress_xcb_keyboard="1"
    fi

    log "Launching candidate platform=${platform} gpu-mode=${candidate_gpu_mode} ${note}"
    log "Session DISPLAY=${DISPLAY:-<unset>} WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>} XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<unset>} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"

    start_ts="$(date +%s)"
    # Force XI2/XKB crash guard + dedicated crash log on xcb (Steam OSK / gamescope)
    local xi2_guard="${AEROCORE_XI2_CRASH_GUARD:-}"
    if [[ "${platform}" == "xcb" && -z "${xi2_guard}" ]]; then
        xi2_guard="1"
    fi
    local xkb_keymap_guard="${AEROCORE_XKB_KEYMAP_GUARD:-}"
    if [[ "${platform}" == "xcb" && -z "${xkb_keymap_guard}" ]]; then
        xkb_keymap_guard="1"
    fi
    export AEROCORE_CRASH_LOG="${AEROCORE_CRASH_LOG:-${STATE_LOG_DIR}/crash.log}"

    if [[ ${#flag_args[@]} -gt 0 ]]; then
        AEROCORE_GPU_MODE="${candidate_gpu_mode}" \
        AEROCORE_SUPPRESS_XCB_KEYBOARD="${suppress_xcb_keyboard}" \
        AEROCORE_XI2_CRASH_GUARD="${xi2_guard}" \
        AEROCORE_XKB_KEYMAP_GUARD="${xkb_keymap_guard}" \
        AEROCORE_CRASH_LOG="${AEROCORE_CRASH_LOG}" \
        QT_QPA_PLATFORM="${platform}" \
        "${APP_PATH}" "${flag_args[@]}" "$@" >>"${PORTABLE_LOG_FILE}" 2>&1
    else
        AEROCORE_GPU_MODE="${candidate_gpu_mode}" \
        AEROCORE_SUPPRESS_XCB_KEYBOARD="${suppress_xcb_keyboard}" \
        AEROCORE_XI2_CRASH_GUARD="${xi2_guard}" \
        AEROCORE_XKB_KEYMAP_GUARD="${xkb_keymap_guard}" \
        AEROCORE_CRASH_LOG="${AEROCORE_CRASH_LOG}" \
        QT_QPA_PLATFORM="${platform}" \
        "${APP_PATH}" "$@" >>"${PORTABLE_LOG_FILE}" 2>&1
    fi
    rc=$?
    end_ts="$(date +%s)"
    elapsed=$((end_ts - start_ts))

    log "Candidate platform=${platform} exited rc=${rc} after ${elapsed}s"
    if [[ ${rc} -eq 134 || ${rc} -eq 139 ]]; then
        log "Recent output before crash:"
        # Prefer crash backtrace block if present
        if grep -q '\[crash\] fatal signal' "${PORTABLE_LOG_FILE}" 2>/dev/null; then
            awk '/\[crash\] fatal signal/{flag=1} flag{print; if(/\[crash\] end of backtrace/) exit}' \
                "${PORTABLE_LOG_FILE}" | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        else
            tail -n 30 "${PORTABLE_LOG_FILE}" | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        fi

        local crash_log="${AEROCORE_CRASH_LOG:-${APP_DIR}/crash.log}"
        if [[ -f "${crash_log}" ]]; then
            log "Contents of ${crash_log}:"
            tail -n 80 "${crash_log}" | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        elif [[ -f /tmp/aerocore-browser-crash.log ]]; then
            log "Contents of /tmp/aerocore-browser-crash.log:"
            tail -n 80 /tmp/aerocore-browser-crash.log | while IFS= read -r line; do
                [[ -n "${line}" ]] || continue
                log "  ${line}"
            done
        fi
        if command -v coredumpctl >/dev/null 2>&1; then
            log "coredumpctl info (latest aerocore-browser, if any):"
            # Match by exe name; also try without filter if empty
            local cd_out
            cd_out="$(coredumpctl info -1 "$(basename "${APP_PATH}")" 2>/dev/null | head -n 80 || true)"
            if [[ -z "${cd_out}" ]]; then
                cd_out="$(coredumpctl info -1 2>/dev/null | head -n 80 || true)"
            fi
            if [[ -n "${cd_out}" ]]; then
                while IFS= read -r line; do
                    [[ -n "${line}" ]] || continue
                    log "  ${line}"
                done <<<"${cd_out}"
            else
                log "  (no matching coredump — check systemd-coredump / ulimit -c)"
            fi
            log "Hint: coredumpctl gdb -1  # for interactive backtrace"
        fi
    elif [[ ${rc} -ne 0 && -z "${AEROCORE_DEBUG_PLUGINS:-}" && "${platform}" == "xcb" ]]; then
        log "Hint: rerun with AEROCORE_DEBUG_PLUGINS=1 to capture the real Qt xcb plugin load error."
    fi
    return "${rc}"
}

clear_stale_cef_singleton

if [[ -n "${AEROCORE_FORCE_PLATFORM:-}" ]]; then
    log "AEROCORE_FORCE_PLATFORM=${AEROCORE_FORCE_PLATFORM}"
    log "AEROCORE_GPU_MODE=${GPU_MODE}"
    run_candidate "${AEROCORE_FORCE_PLATFORM}" "${BASE_CHROMIUM_FLAGS} --ozone-platform-hint=auto" "forced" "${GPU_MODE}" "$@"
    exit $?
fi

declare -a candidates=()

log "Portable launcher started."
log "Configured GPU mode: ${GPU_MODE}"
if [[ -z "${AEROCORE_GPU_MODE:-}" && "${GPU_MODE}" == "auto" ]]; then
    log "Portable launcher default: X11 candidates keep gpu-mode=auto but add --disable-gpu-compositing; Wayland candidates stay on gpu-mode=auto"
fi

append_xcb_candidates() {
    if [[ -z "${DISPLAY:-}" ]]; then
        return
    fi

    local xcb_gpu_mode="${GPU_MODE}"
    local xcb_gpu_flags="${BASE_CHROMIUM_FLAGS}"
    local xcb_gpu_note="xcb-gpu-${xcb_gpu_mode}"
    if [[ -z "${AEROCORE_GPU_MODE:-}" && "${GPU_MODE}" == "auto" ]]; then
        xcb_gpu_flags="${BASE_CHROMIUM_FLAGS} --disable-gpu-compositing"
        xcb_gpu_note="xcb-gpu-${xcb_gpu_mode}-no-compositor"
    fi

    if [[ "${xcb_gpu_mode}" == "x11-egl" || "${xcb_gpu_mode}" == "auto" || "${xcb_gpu_mode}" == "unsafe" ]]; then
        candidates+=("xcb|${xcb_gpu_flags}|${xcb_gpu_note}|${xcb_gpu_mode}")
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS}|xcb-software-fallback|software")
    else
        candidates+=("xcb|${xcb_gpu_flags}|xcb-default-${xcb_gpu_mode}|${xcb_gpu_mode}")
        candidates+=("xcb|${BASE_CHROMIUM_FLAGS} --disable-gpu --disable-software-rasterizer|xcb-fallback-${xcb_gpu_mode}|${xcb_gpu_mode}")
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

if gamescope_session; then
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

    if [[ ${rc} -eq 134 || ${rc} -eq 139 ]]; then
        log "Candidate ${platform} crashed immediately, trying next backend."
        continue
    fi
    exit ${rc}
done

log "All launcher candidates failed. Inspect ${PORTABLE_LOG_FILE} or ${STATE_LOG_FILE}."
exit 1
EOF

    chmod +x "$DIST_DIR/run.sh"
    sed -i 's/\r$//' "$DIST_DIR/run.sh" 2>/dev/null || true
}

create_archive() {
    mkdir -p "$(dirname "$DIST_ARCHIVE")"
    tar -C "$(dirname "$DIST_DIR")" -czf "$DIST_ARCHIVE" "$(basename "$DIST_DIR")"
}

should_create_archive() {
    case "$PACKAGE_ARCHIVE" in
        0|false|FALSE|no|NO)
            return 1
            ;;
    esac
    return 0
}

main() {
    local qt_plugin_dir qt_qml_dir

    if [[ ! -x "$BUILD_DIR/$APP_NAME" ]]; then
        echo "Missing built executable at $BUILD_DIR/$APP_NAME" >&2
        echo "Run 'make build' first." >&2
        exit 1
    fi

    qt_plugin_dir="$(qt_plugins_dir)"
    if [[ -z "$qt_plugin_dir" || ! -d "$qt_plugin_dir" ]]; then
        echo "Unable to locate Qt plugin directory." >&2
        echo "Set QT_PLUGIN_DIR explicitly or ensure qtpaths6 is available." >&2
        exit 2
    fi

    qt_qml_dir="$(qt_qml_dir)"
    if [[ -z "$qt_qml_dir" || ! -d "$qt_qml_dir" ]]; then
        echo "Unable to locate Qt QML import directory." >&2
        echo "Set QT_QML_DIR explicitly or ensure qtpaths6 is available." >&2
        exit 2
    fi

    rm -rf "$DIST_DIR"
    mkdir -p "$DIST_DIR/lib" "$DIST_DIR/plugins" "$DIST_DIR/qml"

    copy_build_payload
    copy_updater
    write_release_manifest
    copy_release_assets
    ensure_config_payload
    install_distribution_profile
    ensure_widevine_payload
    report_widevine_payload
    copy_qt_plugins
    verify_sqlite_driver
    copy_supplemental_homebrew_qt_plugins
    copy_qt_qml_modules
    bundle_non_system_deps
    ensure_svg_runtime_libs
    prune_bundled_x11_libs
    bundle_libqxcb_runtime_deps
    sync_cef_resources_with_libcef
    verify_cef_runtime_resources
    bundle_qt_platform_system_deps
    bundle_qml_plugin_deps
    apply_portable_rpaths
    fix_libqxcb_rpath
    fix_svg_image_plugin_rpath
    fix_qml_plugin_rpaths
    verify_portable_xcb_plugin
    verify_svg_image_plugin
    verify_config_runtime
    verify_qml_controls_plugin
    write_launcher
    if should_create_archive; then
        create_archive
    fi

    echo ">>> Portable bundle created at $DIST_DIR"
    echo ">>> Start with: $DIST_DIR/run.sh --windowed"
    echo ">>> Logs: $DIST_DIR/launcher.log and ~/.local/state/aerocore-browser/launcher.log"
    if should_create_archive; then
        echo ">>> Archive created at $DIST_ARCHIVE"
    else
        echo ">>> Archive creation skipped (PACKAGE_ARCHIVE=$PACKAGE_ARCHIVE)"
    fi
}

main "$@"
