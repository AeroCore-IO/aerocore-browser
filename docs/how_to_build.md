# Linux Build and Packaging

AeroCore Browser targets Linux x86_64 and is built with CMake, Qt 6, SDL2, and CEF. SteamOS and Bazzite users can build and package the application in Desktop Mode, then verify it in Gaming Mode.

## Dependencies

The build requires these tools and development libraries:

- `cmake`
- `gcc`
- `python3`
- `git`
- `qtbase`
- `qtsvg` (the `libqsvg.so` plugin is required to load `.svg` images in QML)
- `sdl2`

On Steam Deck with Homebrew, also install:

- `glibc`
- `linux-headers@6.8`

Recommended install command:

```bash
HOMEBREW_NO_AUTO_UPDATE=1 brew install gcc cmake qtbase qtsvg sdl2 glibc linux-headers@6.8
```

## Getting the CEF SDK

You can prepare CEF in either of these ways:

1. Let the repository `Makefile` download the public Linux CEF binary distribution.
2. Prepare and extract a Linux x86_64 CEF binary distribution manually.

To build the project with the pinned `linux64 stable minimal` CEF SDK, run `make`. The SDK is downloaded to `third_party/cef` and checked against its SHA-1 checksum:

```bash
make
```

The download URL is configured by `CEF_DOWNLOAD_BASE_URL` in `config/cef.mk`. By default, the pinned archive is downloaded from `https://downloads.aerocore.io/cef`. Override the base URL for CI or another archive mirror:

```bash
make CEF_DOWNLOAD_BASE_URL=https://your-server.example.com/cef
```

When hosting your own archive, update `CEF_ARCHIVE_NAME` and `CEF_ARCHIVE_SHA1` together. The archive must come from the same CEF build configured with proprietary codecs.

To use a CEF SDK that you prepared manually, extract it to a directory such as:

```text
/home/deck/sdk/cef/cef_binary_<version>_linux64
```

The extracted directory must contain at least:

- `include/cef_app.h`
- `Release/libcef.so`
- `Resources/`

By default, the repository `Makefile` downloads the pinned public `linux64 stable minimal` CEF SDK to `third_party/cef`. The pinned version is configured in `config/cef.mk`. When updating CEF, update `CEF_ARCHIVE_NAME` and `CEF_ARCHIVE_SHA1` together. To use a manually prepared SDK, set `CEF_ROOT` to its directory.

### Building CEF with H.264/AAC support

Public Linux CEF packages generally support open formats such as WebM, VP8, and VP9, but do not enable the proprietary codecs required for H.264/AAC/MP4. Some video services require MP4 with H.264/AAC, so building CEF with proprietary codecs may be necessary.

The official Linux build guidance recommends at least:

- 16 GB of RAM
- 200 GB of free disk space
- An Ubuntu 20.04+ or Debian 10+ compatible environment
- Chromium build tools including Ninja, Clang, and depot_tools

The repository provides these targets:

```bash
make cef-build-check
make cef-build-proprietary CEF_BUILD_DIR=/path/with/200GB/free/cef-source-build
make cef-install-proprietary CEF_BUILD_DIR=/path/with/200GB/free/cef-source-build
make
```

By default, the build uses branch `7727`, corresponding to CEF/Chromium 147, and these GN definitions:

```bash
GN_DEFINES="is_official_build=true use_sysroot=true symbol_level=1 is_cfi=false use_thin_lto=false chrome_pgo_phase=0 proprietary_codecs=true ffmpeg_branding=Chrome"
```

The default `automate-git.py` arguments follow the public distribution workflow and produce `minimal` and `client` packages. `make cef-install-proprietary` installs the `minimal` package to `third_party/cef` by default.

The default flow is sufficient to link and run AeroCore Browser. To build a more complete `standard`/`full` CEF binary distribution, override `CEF_AUTOMATE_ARGS`. For example:

```bash
make cef-build-proprietary \
	CEF_BUILD_DIR=/path/with/200GB/free/cef-source-build \
	CEF_AUTOMATE_ARGS="--download-dir=/path/with/200GB/free/cef-source-build --branch=7727 --client-distrib --no-chromium-history --no-debug-build --build-target=cefsimple --x64-build"

make cef-install-proprietary \
	CEF_BUILD_DIR=/path/with/200GB/free/cef-source-build \
	CEF_DISTRIB_FLAVOR=standard
```

`CEF_AUTOMATE_ARGS`, when set, replaces the script's default arguments; it does not append to them. The default `--minimal-distrib` option controls whether a minimal package is produced. Omit it to produce `standard`/`full`. `CEF_DISTRIB_FLAVOR` only selects which already-produced package to install; it does not trigger a new package build.

Supported install package types are:

- `CEF_DISTRIB_FLAVOR=minimal`
- `CEF_DISTRIB_FLAVOR=standard`
- `CEF_DISTRIB_FLAVOR=full`
- `CEF_DISTRIB_FLAVOR=client`

Here, `full` is an alias for `standard` and resolves to the standard Linux package name.

If downloads during the build need a proxy, pass a standard environment variable to `make`:

```bash
http_proxy=http://proxy.example.com:8080 make cef-build-proprietary CEF_BUILD_DIR=/path/with/200GB/free/cef-source-build
```

You can also set `https_proxy` or `all_proxy` as needed. Download tools read these variables from the environment; no repository-specific proxy option is required.

For a full CEF source build on Bazzite, place `CEF_BUILD_DIR` on a disk with sufficient free space, for example:

```bash
make cef-build-proprietary CEF_BUILD_DIR=/run/media/$USER/build/cef-source-build
```

## Configure and Build

### Using the Makefile

The direct CMake workflow is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCEF_ROOT=/home/deck/sdk/cef/cef_binary_<version>_linux64
cmake --build build
```

You can also use the repository's top-level `Makefile`:

```bash
make CEF_ROOT=/home/deck/sdk/cef/cef_binary_<version>_linux64
```

Commonly overridden Makefile variables:

| Variable | Purpose |
| --- | --- |
| `BUILD_DIR`, `BUILD_TYPE` | CMake build directory and build type; defaults are `build` and `Debug` |
| `JOBS` | Number of parallel jobs for the application build; default is `4` |
| `CEF_ROOT` | Path to an extracted CEF SDK; defaults to `third_party/cef` |
| `CEF_DOWNLOAD_BASE_URL` | Base URL for the CEF archive, including self-hosted archives |
| `CEF_BUILD_DIR` | CEF source build directory; a source build typically needs about 200 GB of free space |
| `CEF_MIN_FREE_GB` | Minimum free space required before a CEF source build; default is `200` |
| `CEF_FORCE_CLEAN`, `CEF_NINJA_JOBS` | Whether to discard existing CEF source state, and the parallel job count for the CEF build |
| `CEF_INSTALL_SYSROOT`, `CEF_DISTRIB_FLAVOR` | Linux sysroots to install for the CEF build, and the package type to install |
| `PACKAGE_VARIANT` | Initial sites and update source variant: `zh-CN` or `international` |
| `APP_INSTALL_DIR` | Per-user installation directory used by `make install` |

For example, build with eight parallel jobs and the international variant:

```bash
make JOBS=8 PACKAGE_VARIANT=international
```

For a clean rebuild:

```bash
make distclean
make CEF_ROOT=/home/deck/sdk/cef/cef_binary_<version>_linux64
```

To install the Release build over the current user's default installation (`~/.local/share/aerocore-browser`), run:

```bash
make install
```

This target packages the selected variant and updates the per-user installation. An existing installation is updated in place without restarting Steam or rewriting its shortcut. A first-time installation creates a Steam shortcut. If Steam is running, the script asks in an interactive terminal whether to close Steam and register the shortcut. After confirmation, Steam remains closed and must be restarted manually. Installation is aborted if confirmation is declined or cannot be requested interactively. Exit AeroCore Browser before installing. Set `APP_INSTALL_DIR=/path/to/install` to change the installation directory, or `PACKAGE_VARIANT=international` to select the international variant.

The shortest build and run workflow is usually:

```bash
make
./run.sh --windowed
```

To package the application for another SteamOS or Bazzite machine, also run:

```bash
make package-portable
```

## Run

Start normally:

```bash
./run.sh
```

Start in a window for debugging:

```bash
./run.sh --windowed
```

Pass an initial URL:

```bash
./run.sh https://www.bilibili.com
```

The launcher script:

- Writes logs to `~/.local/state/aerocore-browser/launcher.log`.
- Selects `QT_QPA_PLATFORM` based on whether Wayland or X11 is in use.
- Leaves `QT_SCALE_FACTOR` unset by default so Qt 6 can use the system/compositor scale.
- Tries conservative GPU and ozone startup options for CEF.

Scaling has three layers: `systemUiScale`, `appUiScale`, and `webZoom`.

- Qt 6 and the system/compositor determine `systemUiScale`, which is logged at startup.
- `appUiScale` is the application's large-screen UI scale and affects the native Qt interface and HUD.
- The default web zoom is `1.2` (120%) and is stored separately through `QSettings`.
- `webZoom` is stored per site and is independent of `appUiScale`.
- If you set `QT_SCALE_FACTOR` or `QT_SCREEN_SCALE_FACTORS` manually, the launcher passes them through unchanged and logs them for troubleshooting.

To add Chromium/CEF command-line options:

```bash
AEROCORE_CHROMIUM_FLAGS="--disable-gpu-vsync" ./run.sh
```

## Troubleshooting

### CMake reports `CEF_ROOT is not set`

This means `-DCEF_ROOT=...` was not provided, or the default `third_party/cef` directory does not exist.

Set the path to the extracted CEF SDK:

```bash
make CEF_ROOT=/absolute/path/to/cef_binary_<version>_linux64
```

### CMake cannot find `include/cef_app.h` or `Release/libcef.so`

`CEF_ROOT` does not point to the root of the extracted CEF SDK. Confirm that the directory contains `include/`, `Release/`, and `Resources/`.

### `features.h` or `stdlib.h` not found

Homebrew `glibc` or Linux headers may be missing, or external compiler flags may have changed the header search order.

Install the dependencies and rebuild:

```bash
HOMEBREW_NO_AUTO_UPDATE=1 brew install glibc linux-headers@6.8
make distclean
make CEF_ROOT=/absolute/path/to/cef_binary_<version>_linux64
```

Use the repository `Makefile` so its include search order is preserved.

### Qt or Homebrew installation fails repeatedly

- Install `qtbase` and the required `qtsvg` plugin instead of the `qt` meta-package.
- On Linux Homebrew, `libqsvg.so` is installed under `$(brew --prefix)/lib/qt6/plugins/imageformats/`; `make package-portable` copies it into the portable package.
- Keep `HOMEBREW_NO_AUTO_UPDATE=1` set.

### The app exits immediately when launched from Steam as a non-Steam game

- Add `run.sh` to Steam instead of adding the binary directly.
- Check `~/.local/state/aerocore-browser/launcher.log` for errors.
- To force a platform for debugging, set one of the following:

```bash
AEROCORE_FORCE_PLATFORM=wayland /home/deck/workspace/aerocore-browser/run.sh
```

or:

```bash
AEROCORE_FORCE_PLATFORM=xcb /home/deck/workspace/aerocore-browser/run.sh
```

## Minimal Reproduction Workflow

```bash
HOMEBREW_NO_AUTO_UPDATE=1 brew install gcc cmake qtbase qtsvg sdl2 glibc linux-headers@6.8
make info CEF_ROOT=/absolute/path/to/cef_binary_<version>_linux64
make CEF_ROOT=/absolute/path/to/cef_binary_<version>_linux64
./run.sh --windowed
```
