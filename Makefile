# Windows command: cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 & cd /d D:\Workspace\aerocore-browser & cmake --build build-win --target aerocore-browser'
SHELL := /bin/bash
.DEFAULT_GOAL := all

comma := ,
BUILD_DIR ?= build
BUILD_TYPE ?= Debug
PACKAGE_BUILD_DIR ?= build-release
PACKAGE_BUILD_TYPE ?= Release
ASAN_BUILD_DIR ?= build-asan
JOBS ?= 4
DIST_DIR ?= $(CURDIR)/dist/aerocore-browser-portable
PACKAGE_ARCHIVE ?= 1
PACKAGE_VARIANT ?= zh-CN
APPIMAGE_DIR ?= $(CURDIR)/dist/aerocore-browser.AppDir
APPIMAGE_OUTPUT ?= $(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-x86_64.AppImage
APPIMAGETOOL ?= $(CURDIR)/dist/tools/appimagetool-x86_64.AppImage
APPIMAGE_ARCH ?= x86_64
INSTALLER_BUILD_DIR ?= installer/build-release
APP_INSTALL_DIR ?= $(if $(XDG_DATA_HOME),$(XDG_DATA_HOME),$(HOME)/.local/share)/aerocore-browser
INSTALL_DIST_DIR ?= $(CURDIR)/dist/aerocore-browser-install
VERSION ?= 0.1.0
AEROCORE_VERSION ?= $(VERSION)
UPDATER_BUILD ?= OFF
AUTO_UPDATE_CHECK ?= ON
UPDATER_BINARY ?= $(BUILD_DIR)/updater/aerocore-browser-updater
CMAKE ?= cmake
GENERATOR ?= Unix Makefiles
CEF_ROOT ?= $(CURDIR)/third_party/cef
CEF_PLATFORM ?= linux64
CEF_DOWNLOAD_DIR ?= $(CURDIR)/third_party/downloads
include $(CURDIR)/config/cef.mk
CEF_BUILD_DIR ?= $(CURDIR)/third_party/cef-source-build
CEF_BRANCH ?= 7727
CEF_MIN_FREE_GB ?= 200
CEF_FORCE_CLEAN ?= 0
CEF_NINJA_JOBS ?=
CEF_INSTALL_SYSROOT ?= x64

# Linuxbrew installation prefix used by the rpath-link options below. Without brew shellenv,
# it may be unset in a bare shell, causing rpath-link to fall back to /lib:/usr/lib and resolve
# Qt dependencies against system Qt 6.9.1 instead of Homebrew Qt 6.11.0, which can cause private
# ABI linker errors. Configure runs on each build and command-line -D options overwrite cached
# values, so provide a default while allowing environment and command-line overrides.
HOMEBREW_PREFIX ?= /home/linuxbrew/.linuxbrew

SYSTEM_LD := $(firstword $(wildcard /lib64/ld-linux-x86-64.so.2 /usr/lib/ld-linux-x86-64.so.2))
BREW_LD := $(HOMEBREW_PREFIX)/lib/ld.so
SYSTEM_GLIBC_VERSION := $(shell if [[ -x "$(SYSTEM_LD)" ]]; then "$(SYSTEM_LD)" --version 2>/dev/null | sed -n '1{s/.*version \([0-9][0-9.]*[0-9]\).*/\1/p;}'; fi)
BREW_GLIBC_VERSION := $(shell if [[ -x "$(BREW_LD)" ]]; then "$(BREW_LD)" --version 2>/dev/null | sed -n '1{s/.*version \([0-9][0-9.]*[0-9]\).*/\1/p;}'; fi)
SYSTEM_GLIBC_IS_NEWER := $(shell if [[ -n "$(SYSTEM_GLIBC_VERSION)" && -n "$(BREW_GLIBC_VERSION)" ]] && [[ "$$(printf '%s\n' "$(BREW_GLIBC_VERSION)" "$(SYSTEM_GLIBC_VERSION)" | sort -V | tail -n1)" == "$(SYSTEM_GLIBC_VERSION)" ]] && [[ "$(SYSTEM_GLIBC_VERSION)" != "$(BREW_GLIBC_VERSION)" ]]; then echo 1; fi)
SYSTEM_DYNAMIC_LINKER_FLAG := $(if $(SYSTEM_GLIBC_IS_NEWER),-Wl$(comma)--dynamic-linker=$(SYSTEM_LD))

CONFIGURE_ARGS := -S . -B $(BUILD_DIR) -G "$(GENERATOR)"
CONFIGURE_ARGS += -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
CONFIGURE_ARGS += "-DCMAKE_CXX_FLAGS=$(CPPFLAGS)"
CONFIGURE_ARGS += -DCEF_ROOT=$(CEF_ROOT)
CONFIGURE_ARGS += -DVERSION=$(AEROCORE_VERSION)
CONFIGURE_ARGS += -DAEROCORE_BUILD_UPDATER=$(UPDATER_BUILD)
CONFIGURE_ARGS += -DAEROCORE_ENABLE_AUTO_UPDATE_CHECK=$(AUTO_UPDATE_CHECK)
# In a clean environment without LD_LIBRARY_PATH, the Homebrew linker cannot resolve transitive
# dependencies of libraries under /usr/lib (such as libGLX -> libGLdispatch). Search Homebrew
# first to avoid linking against system Qt. rpath-link affects link-time resolution only.
# Keep CEF subprocesses executing the application (rather than an explicitly invoked ld.so):
# the host loader is embedded in the ELF when the host glibc is newer than Homebrew's.
CONFIGURE_ARGS += "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link=$(HOMEBREW_PREFIX)/lib:/usr/lib $(SYSTEM_DYNAMIC_LINKER_FLAG)"

ASAN_CONFIGURE_ARGS := -S . -B $(ASAN_BUILD_DIR) -G "$(GENERATOR)"
ASAN_CONFIGURE_ARGS += -DCMAKE_BUILD_TYPE=Debug
ASAN_CONFIGURE_ARGS += "-DCMAKE_CXX_FLAGS=$(CPPFLAGS)"
ASAN_CONFIGURE_ARGS += -DCEF_ROOT=$(CEF_ROOT)
ASAN_CONFIGURE_ARGS += -DAEROCORE_ENABLE_ASAN=ON
ASAN_CONFIGURE_ARGS += -DAEROCORE_ENABLE_UBSAN=ON
ASAN_CONFIGURE_ARGS += "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link=$(HOMEBREW_PREFIX)/lib:/usr/lib $(SYSTEM_DYNAMIC_LINKER_FLAG)"

.PHONY: all configure configure-asan build build-asan run run-asan install clean distclean info cef-bootstrap cef-build-check cef-build-proprietary cef-install-proprietary package-portable check-appimage-host package-appimage

$(CEF_ROOT)/include/cef_app.h:
	@set -euo pipefail; \
	if [[ -f "$(CEF_ROOT)/include/cef_app.h" && -f "$(CEF_ROOT)/Release/libcef.so" ]]; then \
		exit 0; \
	fi; \
	mkdir -p "$(CEF_DOWNLOAD_DIR)" "$$(dirname "$(CEF_ROOT)")"; \
	archive="$(CEF_ARCHIVE_NAME)"; \
	archive_path="$(CEF_DOWNLOAD_DIR)/$$archive"; \
	echo ">>> Downloading $$archive (fixed CEF version, resumable). This may take several minutes."; \
	echo ">>> Saving to $$archive_path"; \
	curl -L --fail -C - --retry 3 --retry-delay 2 \
		--progress-bar \
		--output "$$archive_path" \
		"$(CEF_DOWNLOAD_BASE_URL)/$$archive"; \
	if command -v sha1sum >/dev/null 2>&1; then \
		echo "$(CEF_ARCHIVE_SHA1)  $$archive_path" | sha1sum --check --status -; \
	else \
		actual_sha1="$$(shasum -a 1 "$$archive_path" | awk '{print $$1}')"; \
		if [[ "$$actual_sha1" != "$(CEF_ARCHIVE_SHA1)" ]]; then \
			echo "CEF archive SHA-1 mismatch: expected $(CEF_ARCHIVE_SHA1), got $$actual_sha1" >&2; \
			exit 1; \
		fi; \
	fi; \
	echo ">>> Extracting CEF SDK ..."; \
	tmpdir="$$(mktemp -d)"; \
	tar -xf "$$archive_path" -C "$$tmpdir"; \
	extracted="$$(find "$$tmpdir" -mindepth 1 -maxdepth 1 -type d -name 'cef_binary_*_$(CEF_PLATFORM)*' | head -n 1)"; \
	if [[ -z "$$extracted" ]]; then \
		echo "Failed to extract CEF archive $$archive" >&2; \
		rm -rf "$$tmpdir"; \
		exit 1; \
	fi; \
	rm -rf "$(CEF_ROOT)"; \
	mv "$$extracted" "$(CEF_ROOT)"; \
	rm -rf "$$tmpdir"

cef-bootstrap: $(CEF_ROOT)/include/cef_app.h

cef-build-check:
	CEF_BUILD_DIR="$(CEF_BUILD_DIR)" \
	CEF_BRANCH="$(CEF_BRANCH)" \
	CEF_MIN_FREE_GB="$(CEF_MIN_FREE_GB)" \
	./scripts/build-cef-proprietary.sh --check-only

cef-build-proprietary:
	CEF_BUILD_DIR="$(CEF_BUILD_DIR)" \
	CEF_BRANCH="$(CEF_BRANCH)" \
	CEF_MIN_FREE_GB="$(CEF_MIN_FREE_GB)" \
	CEF_FORCE_CLEAN="$(CEF_FORCE_CLEAN)" \
	CEF_NINJA_JOBS="$(CEF_NINJA_JOBS)" \
	CEF_INSTALL_SYSROOT="$(CEF_INSTALL_SYSROOT)" \
	./scripts/build-cef-proprietary.sh --build

cef-install-proprietary:
	CEF_BUILD_DIR="$(CEF_BUILD_DIR)" \
	CEF_DISTRIB_FLAVOR="$(CEF_DISTRIB_FLAVOR)" \
	CEF_INSTALL_ROOT="$(CEF_ROOT)" \
	./scripts/build-cef-proprietary.sh --install

all: build

configure: $(CEF_ROOT)/include/cef_app.h
	@set -euo pipefail; \
	if [[ -f "$(BUILD_DIR)/CMakeCache.txt" ]]; then \
		stale=0; \
		for var in CMAKE_C_COMPILER CMAKE_CXX_COMPILER; do \
			cached="$$(sed -n "s/^$${var}:.*=//p" "$(BUILD_DIR)/CMakeCache.txt" | head -n1)"; \
			if [[ -n "$$cached" && ! -x "$$cached" ]]; then \
				echo ">>> Stale $$var in CMake cache ($$cached missing), clearing $(BUILD_DIR)/CMakeCache.txt"; \
				stale=1; \
			fi; \
		done; \
		if [[ "$$stale" == 1 ]]; then rm -f "$(BUILD_DIR)/CMakeCache.txt"; fi; \
	fi
	$(CMAKE) $(CONFIGURE_ARGS)

configure-asan: $(CEF_ROOT)/include/cef_app.h
	@set -euo pipefail; \
	if [[ -f "$(ASAN_BUILD_DIR)/CMakeCache.txt" ]]; then \
		stale=0; \
		for var in CMAKE_C_COMPILER CMAKE_CXX_COMPILER; do \
			cached="$$(sed -n "s/^$${var}:.*=//p" "$(ASAN_BUILD_DIR)/CMakeCache.txt" | head -n1)"; \
			if [[ -n "$$cached" && ! -x "$$cached" ]]; then \
				echo ">>> Stale $$var in CMake cache ($$cached missing), clearing $(ASAN_BUILD_DIR)/CMakeCache.txt"; \
				stale=1; \
			fi; \
		done; \
		if [[ "$$stale" == 1 ]]; then rm -f "$(ASAN_BUILD_DIR)/CMakeCache.txt"; fi; \
	fi
	$(CMAKE) $(ASAN_CONFIGURE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

build-asan: configure-asan
	$(CMAKE) --build $(ASAN_BUILD_DIR) --parallel $(JOBS)

run: build
	./run.sh

run-asan: build-asan
	APP_PATH="$(CURDIR)/$(ASAN_BUILD_DIR)/aerocore-browser" ./run.sh

package-portable: cef-bootstrap
	$(MAKE) --no-print-directory build \
		BUILD_DIR="$(BUILD_DIR)" \
		BUILD_TYPE="$(BUILD_TYPE)" \
		UPDATER_BUILD=ON
	@sed -i 's/\r$$//' scripts/package-portable.sh
	BUILD_DIR="$(BUILD_DIR)" \
	DIST_DIR="$(DIST_DIR)" \
	UPDATER_BINARY="$(UPDATER_BINARY)" \
	PACKAGE_ARCHIVE="$(PACKAGE_ARCHIVE)" \
	PACKAGE_VARIANT="$(PACKAGE_VARIANT)" \
	CEF_DOWNLOAD_BASE_URL="$(CEF_DOWNLOAD_BASE_URL)" \
	CEF_ARCHIVE_NAME="$(CEF_ARCHIVE_NAME)" \
	CEF_ARCHIVE_SHA1="$(CEF_ARCHIVE_SHA1)" \
	./scripts/package-portable.sh

# Build a release portable bundle and install it for the current user. Existing
# installs are updated in place; Steam is only stopped for first-time shortcut
# registration.
install: check-appimage-host
	$(MAKE) --no-print-directory package-portable \
		BUILD_DIR="$(PACKAGE_BUILD_DIR)" \
		BUILD_TYPE="$(PACKAGE_BUILD_TYPE)" \
		DIST_DIR="$(INSTALL_DIST_DIR)" \
		PACKAGE_ARCHIVE=0 \
		PACKAGE_VARIANT="$(PACKAGE_VARIANT)" \
		UPDATER_BUILD=ON \
		PACKAGE_VERSION="$(AEROCORE_VERSION)" \
		UPDATER_BINARY="$(PACKAGE_BUILD_DIR)/updater/aerocore-browser-updater"
	$(CMAKE) -S installer -B "$(INSTALLER_BUILD_DIR)" -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DVERSION="$(AEROCORE_VERSION)" \
		-DCMAKE_INSTALL_PREFIX="$(CURDIR)/$(INSTALLER_BUILD_DIR)/install"
	$(CMAKE) --build "$(INSTALLER_BUILD_DIR)" --target aerocore-steam-shortcut --parallel "$(JOBS)"
	install -d "$(INSTALL_DIST_DIR)/usr/bin" "$(INSTALL_DIST_DIR)/usr/share/aerocore-browser/steam"
	install -m 0755 "$(INSTALLER_BUILD_DIR)/aerocore-steam-shortcut" "$(INSTALL_DIST_DIR)/usr/bin/aerocore-steam-shortcut"
	install -m 0644 src/content/res/logo.png "$(INSTALL_DIST_DIR)/aerocore-browser-steam-icon.png"
	install -m 0644 assets/steam/aerocore-browser-cover.png assets/steam/aerocore-browser-header.png \
		assets/steam/aerocore-browser-preview.png assets/steam/aerocore-browser-hero.png \
		"$(INSTALL_DIST_DIR)/usr/share/aerocore-browser/steam/"
	AEROCORE_INSTALL_DIR="$(APP_INSTALL_DIR)" \
	AEROCORE_PACKAGE_DIR="$(INSTALL_DIST_DIR)" \
	AEROCORE_BROWSER_VERSION="$(AEROCORE_VERSION)" \
	AEROCORE_STEAM_SHORTCUT_HELPER="$(INSTALL_DIST_DIR)/usr/bin/aerocore-steam-shortcut" \
		./scripts/install-built.sh

check-appimage-host:
	@case "$$(uname -s):$$(uname -m)" in \
		Linux:x86_64|Linux:amd64) ;; \
		*) echo "AppImage packaging and local installs require Linux x86_64 (found $$(uname -s):$$(uname -m))." >&2; exit 2 ;; \
	esac

package-appimage: check-appimage-host
	@set -euo pipefail; \
	case "$(PACKAGE_VARIANT)" in zh-CN|international) ;; *) echo "Unsupported PACKAGE_VARIANT: $(PACKAGE_VARIANT)" >&2; exit 2 ;; esac; \
	$(CMAKE) -S installer -B "$(INSTALLER_BUILD_DIR)" -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE=Release \
		-DVERSION="$(AEROCORE_VERSION)" \
		-DCMAKE_INSTALL_PREFIX="$(CURDIR)/$(INSTALLER_BUILD_DIR)/install" \
		; \
	$(CMAKE) --build "$(INSTALLER_BUILD_DIR)" --parallel "$(JOBS)"; \
	rm -rf "$(APPIMAGE_DIR)" "$(APPIMAGE_OUTPUT)"; \
	$(MAKE) --no-print-directory package-portable \
		BUILD_DIR="$(PACKAGE_BUILD_DIR)" \
		BUILD_TYPE="$(PACKAGE_BUILD_TYPE)" \
		DIST_DIR="$(APPIMAGE_DIR)" \
		DIST_ARCHIVE="$(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-v$(AEROCORE_VERSION)_linux-x86_64.tar.gz" \
		PACKAGE_VARIANT="$(PACKAGE_VARIANT)" \
		UPDATER_BUILD=ON \
		PACKAGE_VERSION="$(AEROCORE_VERSION)" \
		UPDATER_BINARY="$(PACKAGE_BUILD_DIR)/updater/aerocore-browser-updater" \
		PACKAGE_ARCHIVE=1; \
	if command -v sha256sum >/dev/null 2>&1; then \
		sha256sum "$(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-v$(AEROCORE_VERSION)_linux-x86_64.tar.gz" > "$(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-v$(AEROCORE_VERSION)_linux-x86_64.tar.gz.sha256"; \
	else \
		shasum -a 256 "$(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-v$(AEROCORE_VERSION)_linux-x86_64.tar.gz" > "$(CURDIR)/dist/AeroCore-Browser-$(PACKAGE_VARIANT)-v$(AEROCORE_VERSION)_linux-x86_64.tar.gz.sha256"; \
	fi; \
	install -m 0755 appimage/AppRun "$(APPIMAGE_DIR)/AppRun"; \
	install -m 0755 appimage/InstalledAppRun "$(APPIMAGE_DIR)/InstalledAppRun"; \
	install -d "$(APPIMAGE_DIR)/usr/bin"; \
	install -m 0755 "$(INSTALLER_BUILD_DIR)/aerocore-browser-installer" "$(APPIMAGE_DIR)/usr/bin/aerocore-browser-installer"; \
	install -D -m 0644 "$(INSTALLER_BUILD_DIR)/locale/zh_CN/LC_MESSAGES/aerocore-browser-installer.mo" "$(APPIMAGE_DIR)/usr/share/locale/zh_CN/LC_MESSAGES/aerocore-browser-installer.mo"; \
	install -m 0755 appimage/install.sh "$(APPIMAGE_DIR)/usr/bin/aerocore-browser-install-payload"; \
	install -d "$(APPIMAGE_DIR)/usr/libexec"; \
	install -m 0755 scripts/ensure-widevine-cdm.sh "$(APPIMAGE_DIR)/usr/libexec/aerocore-browser-ensure-widevine-cdm"; \
	install -m 0755 "$(INSTALLER_BUILD_DIR)/aerocore-steam-shortcut" "$(APPIMAGE_DIR)/usr/bin/aerocore-steam-shortcut"; \
	install -d "$(APPIMAGE_DIR)/usr/share/aerocore-browser/steam"; \
	install -m 0644 src/content/res/logo.png "$(APPIMAGE_DIR)/aerocore-browser-steam-icon.png"; \
	install -m 0644 assets/steam/aerocore-browser-cover.png "$(APPIMAGE_DIR)/usr/share/aerocore-browser/steam/aerocore-browser-cover.png"; \
	install -m 0644 assets/steam/aerocore-browser-header.png "$(APPIMAGE_DIR)/usr/share/aerocore-browser/steam/aerocore-browser-header.png"; \
	install -m 0644 assets/steam/aerocore-browser-preview.png "$(APPIMAGE_DIR)/usr/share/aerocore-browser/steam/aerocore-browser-preview.png"; \
	install -m 0644 assets/steam/aerocore-browser-hero.png "$(APPIMAGE_DIR)/usr/share/aerocore-browser/steam/aerocore-browser-hero.png"; \
	install -m 0644 appimage/io.aerocore.Browser.desktop "$(APPIMAGE_DIR)/io.aerocore.Browser.desktop"; \
	install -m 0644 appimage/io.aerocore.Browser.svg "$(APPIMAGE_DIR)/io.aerocore.Browser.svg"; \
	install -d "$(APPIMAGE_DIR)/usr/share/applications" "$(APPIMAGE_DIR)/usr/share/icons/hicolor/scalable/apps"; \
	install -m 0644 appimage/io.aerocore.Browser.desktop "$(APPIMAGE_DIR)/usr/share/applications/io.aerocore.Browser.desktop"; \
	install -m 0644 appimage/io.aerocore.Browser.svg "$(APPIMAGE_DIR)/usr/share/icons/hicolor/scalable/apps/io.aerocore.Browser.svg"; \
	install -d "$$(dirname "$(APPIMAGETOOL)")"; \
	if [ ! -x "$(APPIMAGETOOL)" ]; then \
		echo ">>> Downloading appimagetool (this may take several minutes)."; \
		echo ">>> Saving to $(APPIMAGETOOL)"; \
		cache_tmp="$(APPIMAGETOOL).tmp.$$"; \
		trap 'rm -f "$$cache_tmp"' EXIT; \
		curl -fL --retry 3 --retry-delay 2 \
			--progress-bar \
			https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage \
			-o "$$cache_tmp"; \
		chmod +x "$$cache_tmp"; \
		mv "$$cache_tmp" "$(APPIMAGETOOL)"; \
	else \
		echo ">>> Using cached appimagetool: $(APPIMAGETOOL)"; \
	fi; \
	env ARCH="$(APPIMAGE_ARCH)" APPIMAGE_EXTRACT_AND_RUN=1 "$(APPIMAGETOOL)" "$(APPIMAGE_DIR)" "$(APPIMAGE_OUTPUT)"; \
	chmod 0755 "$(APPIMAGE_OUTPUT)"; \
	printf 'AppImage created at: %s\n' "$(APPIMAGE_OUTPUT)"

clean:
	@if [ -d "$(BUILD_DIR)" ]; then $(CMAKE) --build $(BUILD_DIR) --target clean; fi

distclean:
	rm -rf $(BUILD_DIR)
	rm -f CMakeCache.txt
	rm -rf CMakeFiles

info:
	@echo CEF_ROOT=$(CEF_ROOT)
	@echo CEF_PLATFORM=$(CEF_PLATFORM)
	@echo CEF_DOWNLOAD_BASE_URL=$(CEF_DOWNLOAD_BASE_URL)
	@echo CEF_ARCHIVE_NAME=$(CEF_ARCHIVE_NAME)
	@echo CEF_ARCHIVE_SHA1=$(CEF_ARCHIVE_SHA1)
	@echo CEF_BUILD_DIR=$(CEF_BUILD_DIR)
	@echo CEF_BRANCH=$(CEF_BRANCH)
	@echo CEF_MIN_FREE_GB=$(CEF_MIN_FREE_GB)
	@echo CEF_FORCE_CLEAN=$(CEF_FORCE_CLEAN)
	@echo CEF_NINJA_JOBS=$(CEF_NINJA_JOBS)
	@echo CEF_INSTALL_SYSROOT=$(CEF_INSTALL_SYSROOT)
	@echo BUILD_TYPE=$(BUILD_TYPE)
	@echo PACKAGE_BUILD_DIR=$(PACKAGE_BUILD_DIR)
	@echo PACKAGE_BUILD_TYPE=$(PACKAGE_BUILD_TYPE)
	@echo INSTALLER_BUILD_DIR=$(INSTALLER_BUILD_DIR)
	@echo DIST_DIR=$(DIST_DIR)
	@echo CPPFLAGS=$(CPPFLAGS)
	@echo CC=$(CC)
	@echo CXX=$(CXX)
	@echo CMAKE=$(CMAKE)
