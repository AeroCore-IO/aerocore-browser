cmake_minimum_required(VERSION 3.21)

set(VERSION "0.1.0" CACHE STRING "AeroCore Browser package version")
project(AeroCoreBrowserInstaller VERSION ${VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Intl REQUIRED)
find_program(MSGFMT_EXECUTABLE NAMES msgfmt REQUIRED)

set(INSTALLER_TRANSLATION_DOMAIN "aerocore-browser-installer")
set(INSTALLER_TRANSLATION_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../locale/zh_CN/LC_MESSAGES/${INSTALLER_TRANSLATION_DOMAIN}.po")
set(INSTALLER_TRANSLATION_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/locale/zh_CN/LC_MESSAGES/${INSTALLER_TRANSLATION_DOMAIN}.mo")

add_custom_command(
    OUTPUT "${INSTALLER_TRANSLATION_OUTPUT}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/zh_CN/LC_MESSAGES"
    COMMAND "${MSGFMT_EXECUTABLE}" --output-file "${INSTALLER_TRANSLATION_OUTPUT}" "${INSTALLER_TRANSLATION_SOURCE}"
    DEPENDS "${INSTALLER_TRANSLATION_SOURCE}"
    VERBATIM
)

add_custom_target(aerocore-browser-installer-translations ALL
    DEPENDS "${INSTALLER_TRANSLATION_OUTPUT}"
)

qt_add_executable(aerocore-browser-installer
    src/main.cpp
)

qt_add_executable(aerocore-steam-shortcut
    ../src/steamshortcut.cpp
)

target_link_libraries(aerocore-browser-installer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Intl::Intl
)

target_link_libraries(aerocore-steam-shortcut PRIVATE
    Qt6::Core
)

target_compile_definitions(aerocore-browser-installer PRIVATE
    AEROCORE_BROWSER_VERSION="${PROJECT_VERSION}"
)

install(FILES "${INSTALLER_TRANSLATION_OUTPUT}"
    DESTINATION "share/locale/zh_CN/LC_MESSAGES"
)

# The installer lives under AppDir/usr/bin while the shared Qt libraries are
# collected under AppDir/lib by scripts/package-portable.sh.
set_target_properties(aerocore-browser-installer PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../../lib"
)

set_target_properties(aerocore-steam-shortcut PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../../lib"
)
