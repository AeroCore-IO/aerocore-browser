# Pinned public CEF binary distribution used by the application build.
#
# When upgrading CEF, update CEF_ARCHIVE_NAME and CEF_ARCHIVE_SHA1 together.
# The archive name and digest are published by the CEF automated builds index:
# https://cef-builds.spotifycdn.com/index.json

# CEF archives are stored under the `cef/` prefix in the shared downloads
# bucket and are served through the downloads.aerocore.io custom domain.
CEF_DOWNLOAD_BASE_URL ?= https://downloads.aerocore.io/cef
CEF_ARCHIVE_NAME ?= cef_binary_151.3.24+g2384915+chromium-151.0.7922.174_linux64_minimal.tar.bz2
CEF_ARCHIVE_SHA1 ?= 7591ec6bd876c1aac7c1d4077a8e158ae84f4b3a
