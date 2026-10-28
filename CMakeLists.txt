cmake_minimum_required(VERSION 3.21)

set(VERSION "0.1.0" CACHE STRING "AeroCore Browser version")
project(AeroCoreBrowser VERSION ${VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(QT_QML_GENERATE_QMLLS_INI ON)

get_property(AEROCORE_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT AEROCORE_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if (WIN32)
    set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "Optional flag specifying which runtime to use" FORCE)
endif()

option(AEROCORE_ENABLE_ASAN "Build AeroCore with AddressSanitizer" OFF)
option(AEROCORE_ENABLE_UBSAN "Build AeroCore with UndefinedBehaviorSanitizer" OFF)
option(AEROCORE_BUILD_UPDATER "Build the user-level AeroCore Browser updater" OFF)
option(AEROCORE_ENABLE_AUTO_UPDATE_CHECK "Check for updates once after startup" ON)

set(CEF_ROOT "$ENV{CEF_ROOT}" CACHE PATH "Path to an extracted CEF binary distribution")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# Vulkan headers are optional; skip probing to avoid configure-time warnings
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)

if (UNIX AND NOT WIN32)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Sql DBus Network WebSockets Qml Svg)
else()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick Sql Qml Svg)
endif()
find_package(SDL2 REQUIRED CONFIG)
find_package(CEF REQUIRED)
find_program(MSGFMT_EXECUTABLE NAMES msgfmt)
find_program(LCONVERT_EXECUTABLE NAMES lconvert HINTS "${Qt6_DIR}/../../../bin" "/usr/lib/qt6/bin" REQUIRED)
find_program(LRELEASE_EXECUTABLE NAMES lrelease HINTS "${Qt6_DIR}/../../../bin" "/usr/lib/qt6/bin" REQUIRED)
find_program(LUPDATE_EXECUTABLE NAMES lupdate HINTS "${Qt6_DIR}/../../../bin" "/usr/lib/qt6/bin" REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(AEROCORE_TRANSLATION_DOMAIN "aerocore-browser")
file(GLOB AEROCORE_TRANSLATION_PO_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/locale/*/LC_MESSAGES/${AEROCORE_TRANSLATION_DOMAIN}.po")
set(AEROCORE_TRANSLATION_OUTPUTS)
set(AEROCORE_TRANSLATION_QM_FILES)
set(AEROCORE_TRANSLATION_MO_FILES)
foreach(AEROCORE_TRANSLATION_PO IN LISTS AEROCORE_TRANSLATION_PO_FILES)
    get_filename_component(AEROCORE_TRANSLATION_MESSAGES_DIR "${AEROCORE_TRANSLATION_PO}" DIRECTORY)
    get_filename_component(AEROCORE_TRANSLATION_LANGUAGE_DIR "${AEROCORE_TRANSLATION_MESSAGES_DIR}" DIRECTORY)
    get_filename_component(AEROCORE_TRANSLATION_LOCALE "${AEROCORE_TRANSLATION_LANGUAGE_DIR}" NAME)
    set(AEROCORE_TRANSLATION_TS "${CMAKE_CURRENT_BINARY_DIR}/${AEROCORE_TRANSLATION_DOMAIN}_${AEROCORE_TRANSLATION_LOCALE}.ts")
    set(AEROCORE_TRANSLATION_QM "${CMAKE_CURRENT_BINARY_DIR}/translations/${AEROCORE_TRANSLATION_DOMAIN}_${AEROCORE_TRANSLATION_LOCALE}.qm")
    list(APPEND AEROCORE_TRANSLATION_QM_FILES "${AEROCORE_TRANSLATION_QM}")
    list(APPEND AEROCORE_TRANSLATION_OUTPUTS "${AEROCORE_TRANSLATION_QM}")
    set(AEROCORE_TRANSLATION_COMMANDS)
    if (MSGFMT_EXECUTABLE)
        set(AEROCORE_TRANSLATION_MO "${CMAKE_CURRENT_BINARY_DIR}/locale/${AEROCORE_TRANSLATION_LOCALE}/LC_MESSAGES/${AEROCORE_TRANSLATION_DOMAIN}.mo")
        list(APPEND AEROCORE_TRANSLATION_MO_FILES "${AEROCORE_TRANSLATION_MO}")
        list(APPEND AEROCORE_TRANSLATION_OUTPUTS "${AEROCORE_TRANSLATION_MO}")
        list(APPEND AEROCORE_TRANSLATION_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/locale/${AEROCORE_TRANSLATION_LOCALE}/LC_MESSAGES"
            COMMAND "${MSGFMT_EXECUTABLE}" --output-file "${AEROCORE_TRANSLATION_MO}" "${AEROCORE_TRANSLATION_PO}"
        )
    endif()
    add_custom_command(
        OUTPUT "${AEROCORE_TRANSLATION_QM}" ${AEROCORE_TRANSLATION_MO}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/translations"
        COMMAND "${LCONVERT_EXECUTABLE}" -i "${AEROCORE_TRANSLATION_PO}" -o "${AEROCORE_TRANSLATION_TS}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/qt-ts-contexts.py"
                "${AEROCORE_TRANSLATION_TS}" "${AEROCORE_TRANSLATION_TS}"
        COMMAND "${LRELEASE_EXECUTABLE}" "${AEROCORE_TRANSLATION_TS}" -qm "${AEROCORE_TRANSLATION_QM}"
        ${AEROCORE_TRANSLATION_COMMANDS}
        DEPENDS "${AEROCORE_TRANSLATION_PO}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/qt-ts-contexts.py"
        VERBATIM
    )
endforeach()
add_custom_target(aerocore-browser-translations ALL
    DEPENDS ${AEROCORE_TRANSLATION_OUTPUTS}
)
if (AEROCORE_BUILD_UPDATER)
    add_subdirectory(updater)
endif()

if (UNIX AND NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(X11_XTEST REQUIRED IMPORTED_TARGET xtst x11 xfixes)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
endif()

if (NOT CEF_ROOT)
    message(FATAL_ERROR "CEF_ROOT is not set. Download a Linux CEF binary distribution and configure with -DCEF_ROOT=/absolute/path/to/cef_binary_<version>_linux64.")
endif()

if (NOT EXISTS "${CEF_ROOT}/include/cef_app.h")
    message(FATAL_ERROR "CEF headers not found under ${CEF_ROOT}/include. Check that CEF_ROOT points at the root of the extracted CEF SDK.")
endif()

if (WIN32)
    set(CEF_RUNTIME_LIBRARY "${CEF_ROOT}/Release/libcef.dll")
    set(CEF_LINK_LIBRARY "${CEF_ROOT}/Release/libcef.lib")
    set(CEF_PLATFORM_HINT "Windows")
else()
    set(CEF_RUNTIME_LIBRARY "${CEF_ROOT}/Release/libcef.so")
    set(CEF_LINK_LIBRARY "${CEF_ROOT}/Release/libcef.so")
    set(CEF_PLATFORM_HINT "Linux")
endif()

if (NOT EXISTS "${CEF_RUNTIME_LIBRARY}")
    message(FATAL_ERROR "CEF runtime library not found at ${CEF_RUNTIME_LIBRARY}. Check that CEF_ROOT points at a ${CEF_PLATFORM_HINT} CEF binary distribution.")
endif()

if (WIN32 AND NOT EXISTS "${CEF_LINK_LIBRARY}")
    message(FATAL_ERROR "CEF import library not found at ${CEF_LINK_LIBRARY}. Check that CEF_ROOT points at a ${CEF_PLATFORM_HINT} CEF binary distribution.")
endif()

qt_standard_project_setup(REQUIRES 6.5)

ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_RELEASE}" "${CEF_LIB_RELEASE}")
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

set(AEROCORE_SANITIZER_COMPILE_FLAGS)
set(AEROCORE_SANITIZER_LINK_FLAGS)
if (AEROCORE_ENABLE_ASAN OR AEROCORE_ENABLE_UBSAN)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "AEROCORE_ENABLE_ASAN/AEROCORE_ENABLE_UBSAN require Clang or GCC")
    endif()

    list(APPEND AEROCORE_SANITIZER_COMPILE_FLAGS -fno-omit-frame-pointer -g)
    if (AEROCORE_ENABLE_ASAN)
        list(APPEND AEROCORE_SANITIZER_COMPILE_FLAGS -fsanitize=address)
        list(APPEND AEROCORE_SANITIZER_LINK_FLAGS -fsanitize=address)
    endif()
    if (AEROCORE_ENABLE_UBSAN)
        list(APPEND AEROCORE_SANITIZER_COMPILE_FLAGS -fsanitize=undefined -fno-sanitize-recover=undefined)
        list(APPEND AEROCORE_SANITIZER_LINK_FLAGS -fsanitize=undefined -fno-sanitize-recover=undefined)
    endif()
endif()

file(GLOB CEF_RUNTIME_FILES "${CEF_ROOT}/Release/*")
set(CEF_RESOURCE_DIR "${CEF_ROOT}/Resources")
set(CONTROLLER_MAPPING_FILES
    assets/gamecontrollerdb.txt
    assets/gamecontrollerdb.LICENSE.txt
)

add_library(aerocore-website STATIC
    src/database/databasemanager.cpp
    src/database/databasemanager.h
    src/website/website.cpp
    src/website/website.h
    src/website/websitevalidator.cpp
    src/website/websitevalidator.h
    src/website/websiterepository.cpp
    src/website/websiterepository.h
    src/website/websitepresets.cpp
    src/website/websitepresets.h
    src/website/websiteiconstorage.cpp
    src/website/websiteiconstorage.h
    src/website/websiteiconfetcher.cpp
    src/website/websiteiconfetcher.h
    src/website/sessionsitecatalog.h
    src/website/websitesessioncatalog.cpp
    src/website/websitesessioncatalog.h
    src/history/historyitem.cpp
    src/history/historyitem.h
    src/history/historyrepository.cpp
    src/history/historyrepository.h
    src/history/historythumbnailstorage.cpp
    src/history/historythumbnailstorage.h
)
target_link_libraries(aerocore-website PUBLIC Qt6::Sql Qt6::Core Qt6::Gui Qt6::Network)
target_include_directories(aerocore-website PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

qt_add_executable(aerocore-browser
    src/main.cpp
    src/browsercontroller.cpp
    src/browsercontroller.h
    src/browserview.cpp
    src/browserview.h
    src/browserinpututils.h
    src/cursorutils.cpp
    src/cursorutils.h
    src/bookmarkmodel.cpp
    src/bookmarkmodel.h
    src/historymodel.cpp
    src/historymodel.h
    src/inputmanager.cpp
    src/inputmanager.h
    src/gamepadcatalog.cpp
    src/gamepadcatalog.h
    src/powerinhibitor.cpp
    src/powerinhibitor.h
    src/sessionmanager.cpp
    src/sessionmanager.h
    src/sessionpersistence.cpp
    src/sessionpersistence.h
    src/siteconfigmanager.cpp
    src/siteconfigmanager.h
    src/inputmanageradapter.h
    src/inputmanageradapter.cpp
    src/maincontroller.cpp
    src/maincontroller_browser_binding.cpp
    src/maincontroller.h
    src/updatecontroller.cpp
    src/updatecontroller.h
    src/buttoniconlayout.cpp
    src/buttoniconlayout.h
    src/memoryimageprovider.cpp
    src/memoryimageprovider.h
    src/setting/cefpaths.cpp
    src/setting/cefpaths.h
    src/setting/cefsettingsservice.cpp
    src/setting/cefsettingsservice.h
    src/setting/cefsettingsservice_clear.cpp
    src/setting/settingscontroller.cpp
    src/setting/settingscontroller.h
    src/setting/settingscontroller_browser.cpp
    src/setting/settingsstore.cpp
    src/setting/settingsstore.h
    src/logging/applogger.cpp
    src/logging/applogger.h
    src/logging/stallwatchdog.cpp
    src/logging/stallwatchdog.h
)
add_dependencies(aerocore-browser aerocore-browser-translations)
add_custom_command(TARGET aerocore-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:aerocore-browser>/translations"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AEROCORE_TRANSLATION_QM_FILES} "$<TARGET_FILE_DIR:aerocore-browser>/translations"
    VERBATIM
)

if (UNIX AND NOT WIN32)
    target_sources(aerocore-browser PRIVATE
        src/steamidleoverrideclient.cpp
        src/steamidleoverrideclient.h
        src/xcbxi2crashguard.cpp
        src/xcbxi2crashguard.h
    )
endif()

if (WIN32)
    target_compile_definitions(aerocore-browser PRIVATE SDL_MAIN_HANDLED)
endif()

if (UNIX AND NOT WIN32)
    qt_add_executable(aerocore-steam-idle-helper
        src/steamidleoverridehelper.cpp
        src/steamidleoverrideadaptors.cpp
        src/steamidleoverrideadaptors.h
    )

    qt_add_executable(aerocore-steam-shortcut
        src/steamshortcut.cpp
    )
endif()

target_include_directories(aerocore-browser PRIVATE
    "${CEF_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if (AEROCORE_SANITIZER_COMPILE_FLAGS)
    target_compile_options(libcef_dll_wrapper PRIVATE ${AEROCORE_SANITIZER_COMPILE_FLAGS})
    target_compile_options(aerocore-browser PRIVATE ${AEROCORE_SANITIZER_COMPILE_FLAGS})
    target_link_options(aerocore-browser PRIVATE ${AEROCORE_SANITIZER_LINK_FLAGS})
endif()

if (COMMAND qt_policy AND Qt6_VERSION VERSION_GREATER_EQUAL "6.8")
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_qml_module(aerocore-browser
    URI AeroCoreBrowser
    QML_FILES src/content/qml/MainWindow.qml
    QML_FILES src/content/qml/base/WindowBase.qml
    QML_FILES src/content/qml/base/ImageButton.qml
    RESOURCES src/content/res/nav-add.svg src/content/res/nav-add-hover.svg src/content/res/nav-help.svg src/content/res/nav-help-hover.svg src/content/res/nav-history.svg src/content/res/nav-history-hover.svg src/content/res/nav-open.svg src/content/res/nav-open-hover.svg src/content/res/nav-settings.svg src/content/res/nav-settings-hover.svg src/content/res/nav-share.svg src/content/res/nav-share-hover.svg src/content/res/nav-quit.svg src/content/res/nav-quit-hover.svg
    RESOURCES src/content/res/logo.png
    QML_FILES src/content/qml/HomePage.qml
    QML_FILES src/content/qml/AddPage.qml
    QML_FILES src/content/qml/OpenPage.qml
    QML_FILES src/content/qml/JoystickCursor.qml
    RESOURCES src/content/res/last-page.svg src/content/res/next-page.svg
    RESOURCES src/content/res/nav-home.svg src/content/res/nav-home-hover.svg
    SOURCES src/browsercontainerquickitem.cpp src/browsercontainerquickitem.h
    QML_FILES src/content/qml/BrowserPage.qml
    QML_FILES src/content/qml/OperateBar.qml
    RESOURCES src/content/res/earth.svg src/content/res/go.svg
    QML_FILES src/content/qml/base/TextFieldBase.qml
    QML_FILES src/content/qml/PageBase.qml
    QML_FILES src/content/qml/base/TextButton.qml
    QML_FILES src/content/qml/base/FocusInterface.qml
    QML_FILES src/content/qml/base/MessagePanel.qml
    RESOURCES src/content/res/message-error.svg src/content/res/message-success.svg
    QML_FILES src/content/qml/base/AppUtils.qml
    QML_FILES src/content/qml/base/MenuPanel.qml
    QML_FILES src/content/qml/base/AerocoreQrPanel.qml
    RESOURCES src/content/res/aerocore-logo.svg src/content/res/aerocore-wechat-qr.png
    RESOURCES src/content/res/has-open.svg src/content/res/current.svg src/content/res/website-default-icon.svg
    QML_FILES src/content/qml/HistoryPage.qml
    RESOURCES src/content/res/history.svg src/content/res/pull-down.svg
    RESOURCES src/content/res/play.svg
    QML_FILES src/content/qml/SettingPage.qml
    RESOURCES src/content/res/minus.svg src/content/res/plus.svg src/content/res/display-setting.svg src/content/res/setting-input.svg src/content/res/setting-privacy.svg src/content/res/setting-system.svg
    QML_FILES src/content/qml/base/ScrollViewBase.qml
    QML_FILES src/content/qml/HelpPage.qml
    QML_FILES src/content/qml/AboutPage.qml
    QML_FILES src/content/qml/hud/ScaleSettingPage.qml
    QML_FILES src/content/qml/hud/FontSizePage.qml
    QML_FILES src/content/qml/hud/HudPage.qml
    QML_FILES src/content/qml/base/RotatingImagePlayer.qml
    RESOURCES src/content/res/spiner-fg.svg src/content/res/spiner-bg.svg
    QML_FILES src/content/qml/hud/HudPanel.qml
    RESOURCES src/content/res/video-default-icon.svg src/content/res/website-default-icon2.svg
    RESOURCES src/content/res/cursor-hand.svg src/content/res/cursor-pointer.svg
)

target_link_libraries(aerocore-browser PRIVATE
    Qt6::Quick
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    aerocore-website
    SDL2::SDL2
    libcef_lib
    libcef_dll_wrapper
)

target_compile_definitions(aerocore-browser PRIVATE
    AEROCORE_BROWSER_VERSION="${PROJECT_VERSION}"
)

if (AEROCORE_ENABLE_AUTO_UPDATE_CHECK)
    target_compile_definitions(aerocore-browser PRIVATE AEROCORE_ENABLE_AUTO_UPDATE_CHECK)
endif()

if (UNIX AND NOT WIN32)
    target_link_libraries(aerocore-browser PRIVATE PkgConfig::X11_XTEST PkgConfig::XCB Qt6::DBus Qt6::Network)
    target_compile_definitions(aerocore-browser PRIVATE AEROCORE_HAVE_X11_SYSTEM_MOUSE)

    target_link_libraries(aerocore-steam-idle-helper PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::DBus
        Qt6::WebSockets
    )

    target_link_libraries(aerocore-steam-shortcut PRIVATE
        Qt6::Core
    )
endif()

if (WIN32)
    target_link_libraries(aerocore-browser PRIVATE delayimp.lib PowrProf.lib)
endif()

SET_EXECUTABLE_TARGET_PROPERTIES(aerocore-browser)

set_target_properties(aerocore-browser PROPERTIES
    BUILD_RPATH "\$ORIGIN:\$ORIGIN/lib"
    INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib"
    BUILD_RPATH_USE_ORIGIN TRUE
    INSTALL_RPATH_USE_LINK_PATH FALSE
)

if (UNIX AND NOT WIN32)
    set_target_properties(aerocore-steam-idle-helper PROPERTIES
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib"
    )

    set_target_properties(aerocore-steam-shortcut PROPERTIES
        BUILD_RPATH "\$ORIGIN"
        INSTALL_RPATH "\$ORIGIN:\$ORIGIN/../lib"
    )
endif()

add_custom_command(TARGET aerocore-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CEF_RESOURCE_DIR}" $<TARGET_FILE_DIR:aerocore-browser>
    VERBATIM
)

foreach(runtime_file IN LISTS CEF_RUNTIME_FILES)
    get_filename_component(runtime_name "${runtime_file}" NAME)
    add_custom_command(TARGET aerocore-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${runtime_file}" $<TARGET_FILE_DIR:aerocore-browser>/${runtime_name}
        VERBATIM
    )
endforeach()

# Widevine CDM（专有组件，提取自 Chrome deb）：随包分发到可执行文件旁，
# 启动时由 main.cpp 的 hint 文件逻辑定位加载。缺失时跳过（仅影响 DRM 播放）。
set(AEROCORE_WIDEVINE_CDM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/widevine/WidevineCdm")
if (EXISTS "${AEROCORE_WIDEVINE_CDM_DIR}/manifest.json")
    add_custom_command(TARGET aerocore-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${AEROCORE_WIDEVINE_CDM_DIR}" $<TARGET_FILE_DIR:aerocore-browser>/WidevineCdm
        VERBATIM
    )
else()
    message(STATUS "Widevine CDM not found at ${AEROCORE_WIDEVINE_CDM_DIR}; DRM playback will be unavailable")
endif()

foreach(mapping_file IN LISTS CONTROLLER_MAPPING_FILES)
    get_filename_component(mapping_name "${mapping_file}" NAME)
    add_custom_command(TARGET aerocore-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/${mapping_file}" $<TARGET_FILE_DIR:aerocore-browser>/${mapping_name}
        VERBATIM
    )
endforeach()

add_custom_command(TARGET aerocore-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/website-icons" $<TARGET_FILE_DIR:aerocore-browser>/assets/website-icons
    VERBATIM
)

add_custom_command(TARGET aerocore-browser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/config" $<TARGET_FILE_DIR:aerocore-browser>/config
    VERBATIM
)

if (UNIX AND NOT WIN32)
    add_custom_command(TARGET aerocore-browser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:aerocore-steam-idle-helper> $<TARGET_FILE_DIR:aerocore-browser>/$<TARGET_FILE_NAME:aerocore-steam-idle-helper>
        VERBATIM
    )
endif()

# =============================================
# 部署配置 (Deployment Configuration)
# =============================================

# 设置安装前缀为绝对路径为：项目根目录下/install/AeroCoreBrowser
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/../../install/${PROJECT_NAME}" CACHE PATH "Install path" FORCE)

# 安装主程序
install(TARGETS aerocore-browser
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(FILES ${AEROCORE_TRANSLATION_QM_FILES}
    DESTINATION bin/translations
)
if (AEROCORE_TRANSLATION_MO_FILES)
    foreach(AEROCORE_TRANSLATION_MO IN LISTS AEROCORE_TRANSLATION_MO_FILES)
        get_filename_component(AEROCORE_TRANSLATION_MESSAGES_DIR "${AEROCORE_TRANSLATION_MO}" DIRECTORY)
        string(REGEX REPLACE ".*/locale/" "" AEROCORE_TRANSLATION_LOCALE_MESSAGES "${AEROCORE_TRANSLATION_MESSAGES_DIR}")
        install(FILES "${AEROCORE_TRANSLATION_MO}" DESTINATION "share/locale/${AEROCORE_TRANSLATION_LOCALE_MESSAGES}")
    endforeach()
endif()

add_custom_target(update-aerocore-browser-po
    COMMENT "Extract translatable Qt strings and update gettext PO catalogs"
)
foreach(AEROCORE_TRANSLATION_PO IN LISTS AEROCORE_TRANSLATION_PO_FILES)
    get_filename_component(AEROCORE_TRANSLATION_MESSAGES_DIR "${AEROCORE_TRANSLATION_PO}" DIRECTORY)
    get_filename_component(AEROCORE_TRANSLATION_LANGUAGE_DIR "${AEROCORE_TRANSLATION_MESSAGES_DIR}" DIRECTORY)
    get_filename_component(AEROCORE_TRANSLATION_LOCALE "${AEROCORE_TRANSLATION_LANGUAGE_DIR}" NAME)
    set(AEROCORE_TRANSLATION_UPDATE_TS "${CMAKE_CURRENT_BINARY_DIR}/${AEROCORE_TRANSLATION_DOMAIN}_${AEROCORE_TRANSLATION_LOCALE}_update.ts")
    add_custom_command(TARGET update-aerocore-browser-po
        POST_BUILD
        COMMAND "${LCONVERT_EXECUTABLE}" -i "${AEROCORE_TRANSLATION_PO}" -o "${AEROCORE_TRANSLATION_UPDATE_TS}"
        COMMAND "${LUPDATE_EXECUTABLE}" ${CMAKE_CURRENT_SOURCE_DIR}/src -ts "${AEROCORE_TRANSLATION_UPDATE_TS}"
        COMMAND "${LCONVERT_EXECUTABLE}" -i "${AEROCORE_TRANSLATION_UPDATE_TS}" -o "${AEROCORE_TRANSLATION_PO}"
        VERBATIM
    )
endforeach()

# 安装配置文件
install(DIRECTORY "${CMAKE_INSTALL_PREFIX}/../../config"
    DESTINATION bin
)

# 安装CEF
install(DIRECTORY "${CEF_ROOT}/Release/"
    DESTINATION bin
)
install(DIRECTORY "${CEF_RESOURCE_DIR}/"
    DESTINATION bin
)

# 安装游戏手柄映射文件
install(FILES ${CONTROLLER_MAPPING_FILES}
    DESTINATION bin
)

install(DIRECTORY assets/website-icons
    DESTINATION bin/assets
)

# 安装 steam-idle-helper
if(UNIX AND NOT WIN32)
    install(TARGETS aerocore-steam-idle-helper
        RUNTIME DESTINATION bin
    )
    install(TARGETS aerocore-steam-shortcut
        RUNTIME DESTINATION bin
    )
endif()

# 使用 Qt 官方的部署 API 自动收集所有 Qt 依赖
qt_generate_deploy_app_script(
    TARGET aerocore-browser
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

qt_generate_deploy_qml_app_script(
    TARGET aerocore-browser
    OUTPUT_SCRIPT deploy_qml_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_qml_script})

# 安装完成后，删除 lib/ 目录下的 libcef 相关文件
install(CODE "
    message(STATUS \"Cleaning up duplicated CEF libraries...\")
    set(LIB_DIR \"\${CMAKE_INSTALL_PREFIX}/lib\")
    if(EXISTS \"\${LIB_DIR}\")
        file(GLOB CEF_LIBS \"\${LIB_DIR}/libcef*.so*\")
        if(CEF_LIBS)
            file(REMOVE \${CEF_LIBS})
            message(STATUS \"Removed duplicated CEF libraries: \${CEF_LIBS}\")
        else()
            message(STATUS \"No CEF libraries found in lib/\")
        endif()
    endif()
")

option(AEROCORE_BUILD_TESTS "Build unit tests" ON)
if (AEROCORE_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)

    set(AEROCORE_REPOSITORY_TEST_TARGETS
        tst_websitevalidator
        tst_websiterepository
        tst_websitesessioncatalog
        tst_websiteiconfetcher
        tst_historyrepository
        tst_historythumbnailstorage
    )

    foreach(test_target IN LISTS AEROCORE_REPOSITORY_TEST_TARGETS)
        qt_add_executable(${test_target} tests/${test_target}.cpp)
        target_link_libraries(${test_target} PRIVATE
            aerocore-website
            Qt6::Test
            Qt6::Network
        )
        target_compile_definitions(${test_target} PRIVATE AEROCORE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()

    qt_add_executable(tst_gamepadcatalog tests/tst_gamepadcatalog.cpp)
    target_sources(tst_gamepadcatalog PRIVATE
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
    )
    target_include_directories(tst_gamepadcatalog PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(tst_gamepadcatalog PRIVATE
        Qt6::Test
        Qt6::Core
    )
    add_test(NAME tst_gamepadcatalog COMMAND tst_gamepadcatalog)

    qt_add_executable(tst_buttoniconlayout tests/tst_buttoniconlayout.cpp)
    target_sources(tst_buttoniconlayout PRIVATE
        src/buttoniconlayout.cpp
        src/buttoniconlayout.h
    )
    target_include_directories(tst_buttoniconlayout PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(tst_buttoniconlayout PRIVATE
        Qt6::Test
        Qt6::Core
        Qt6::Gui
        SDL2::SDL2
    )
    add_test(NAME tst_buttoniconlayout COMMAND tst_buttoniconlayout)

    set(AEROCORE_MAINCONTROLLER_TEST_SOURCES
        src/maincontroller.cpp
        src/maincontroller.h
        src/buttoniconlayout.cpp
        src/buttoniconlayout.h
        src/sessionmanager.cpp
        src/sessionmanager.h
        src/sessionpersistence.cpp
        src/sessionpersistence.h
        tests/stub_maincontroller_browser_binding.cpp
        tests/stub_settingscontroller_browser.cpp
        src/browsercontroller.cpp
        src/browsercontroller.h
        src/inputmanager.cpp
        src/inputmanager.h
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
        src/setting/settingsstore.cpp
        src/setting/settingsstore.h
        src/siteconfigmanager.cpp
        src/siteconfigmanager.h
        src/cursorutils.cpp
        src/cursorutils.h
        tests/stub_browserview_browser_container.cpp
        src/browserview.h
        src/browserview_test_access.h
        src/browserinpututils.h
    )

    foreach(test_target IN ITEMS tst_maincontroller_website tst_maincontroller_history tst_maincontroller_session_action)
        qt_add_executable(${test_target} tests/${test_target}.cpp)
        target_sources(${test_target} PRIVATE ${AEROCORE_MAINCONTROLLER_TEST_SOURCES})
        target_include_directories(${test_target} PRIVATE
            "${CEF_ROOT}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
        target_compile_definitions(${test_target} PRIVATE AEROCORE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${test_target} PRIVATE
            aerocore-website
            Qt6::Test
            Qt6::Gui
            Qt6::Widgets
            Qt6::Quick
            Qt6::Qml
            Qt6::Network
            SDL2::SDL2
        )
        if (UNIX AND NOT WIN32)
            target_link_libraries(${test_target} PRIVATE PkgConfig::X11_XTEST)
            target_compile_definitions(${test_target} PRIVATE AEROCORE_HAVE_X11_SYSTEM_MOUSE)
        endif()
        add_test(NAME ${test_target} COMMAND ${test_target})
    endforeach()

    qt_add_executable(tst_session_navigate tests/tst_session_navigate.cpp)
    target_sources(tst_session_navigate PRIVATE
        src/sessionmanager.cpp
        src/sessionmanager.h
    )
    target_include_directories(tst_session_navigate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(tst_session_navigate PRIVATE aerocore-website Qt6::Test Qt6::Core)
    add_test(NAME tst_session_navigate COMMAND tst_session_navigate)

    qt_add_executable(tst_sessionpersistence tests/tst_sessionpersistence.cpp)
    target_sources(tst_sessionpersistence PRIVATE
        src/sessionmanager.cpp
        src/sessionmanager.h
        src/sessionpersistence.cpp
        src/sessionpersistence.h
    )
    target_include_directories(tst_sessionpersistence PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(tst_sessionpersistence PRIVATE aerocore-website Qt6::Test Qt6::Core)
    add_test(NAME tst_sessionpersistence COMMAND tst_sessionpersistence)

    qt_add_executable(tst_settingsstore tests/tst_settingsstore.cpp)
    target_sources(tst_settingsstore PRIVATE
        src/setting/settingsstore.cpp
        src/setting/settingsstore.h
        src/siteconfigmanager.cpp
        src/siteconfigmanager.h
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
    )
    target_include_directories(tst_settingsstore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(tst_settingsstore PRIVATE Qt6::Test Qt6::Gui)
    add_test(NAME tst_settingsstore COMMAND tst_settingsstore)

    qt_add_executable(tst_cefsettingsservice tests/tst_cefsettingsservice.cpp)
    target_sources(tst_cefsettingsservice PRIVATE
        src/setting/cefpaths.cpp
        src/setting/cefpaths.h
        src/setting/settingsstore.cpp
        src/setting/settingsstore.h
        src/siteconfigmanager.cpp
        src/siteconfigmanager.h
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
    )
    target_include_directories(tst_cefsettingsservice PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(tst_cefsettingsservice PRIVATE Qt6::Test Qt6::Gui)
    add_test(NAME tst_cefsettingsservice COMMAND tst_cefsettingsservice)

    qt_add_executable(tst_settingscontroller tests/tst_settingscontroller.cpp)
    target_sources(tst_settingscontroller PRIVATE
        src/setting/cefpaths.cpp
        src/setting/cefpaths.h
        src/setting/cefsettingsservice.cpp
        src/setting/cefsettingsservice.h
        tests/stub_cefsettingsservice_clear.cpp
        src/setting/settingscontroller.cpp
        src/setting/settingscontroller.h
        tests/stub_settingscontroller_browser.cpp
        ${AEROCORE_MAINCONTROLLER_TEST_SOURCES}
    )
    target_include_directories(tst_settingscontroller PRIVATE
        "${CEF_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(tst_settingscontroller PRIVATE
        aerocore-website
        Qt6::Test
        Qt6::Gui
        Qt6::Widgets
        Qt6::Quick
        Qt6::Qml
        SDL2::SDL2
    )
    if (UNIX AND NOT WIN32)
        target_link_libraries(tst_settingscontroller PRIVATE PkgConfig::X11_XTEST)
        target_compile_definitions(tst_settingscontroller PRIVATE AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    endif()
    add_test(NAME tst_settingscontroller COMMAND tst_settingscontroller)
    set_tests_properties(tst_settingscontroller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(tst_inputmanager tests/tst_inputmanager.cpp)
    target_sources(tst_inputmanager PRIVATE
        src/inputmanager.cpp
        src/inputmanager.h
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
        src/setting/settingsstore.cpp
        src/setting/settingsstore.h
        src/siteconfigmanager.cpp
        src/siteconfigmanager.h
        src/inputmanageradapter.cpp
        src/inputmanageradapter.h
        src/cursorutils.cpp
        src/cursorutils.h
        tests/stub_browserview_browser_container.cpp
        src/browserview.h
        src/browserview_test_access.h
        src/browserinpututils.h
    )
    target_include_directories(tst_inputmanager PRIVATE
        "${CEF_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(tst_inputmanager PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Widgets
        Qt6::Quick
        SDL2::SDL2
    )
    if (UNIX AND NOT WIN32)
        target_link_libraries(tst_inputmanager PRIVATE PkgConfig::X11_XTEST)
        target_compile_definitions(tst_inputmanager PRIVATE AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    endif()
    add_test(NAME tst_inputmanager COMMAND tst_inputmanager)
    set_tests_properties(tst_inputmanager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(tst_browsercontainer_isloading tests/tst_browsercontainer_isloading.cpp)
    target_sources(tst_browsercontainer_isloading PRIVATE
        src/browsercontainerquickitem.cpp
        src/browsercontainerquickitem.h
        tests/stub_browserview_browser_container.cpp
        src/browserview.h
        src/browserview_test_access.h
        src/browsercontroller.cpp
        src/browsercontroller.h
        tests/stub_inputmanager_browser_container.cpp
        src/inputmanager.h
        src/setting/settingsstore.cpp
        src/setting/settingsstore.h
        src/gamepadcatalog.cpp
        src/gamepadcatalog.h
        src/siteconfigmanager.cpp
        src/siteconfigmanager.h
        src/history/historyitem.cpp
        src/history/historyitem.h
        tests/stub_maincontroller_browser_container.cpp
    )
    target_include_directories(tst_browsercontainer_isloading PRIVATE
        "${CEF_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    target_link_libraries(tst_browsercontainer_isloading PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Quick
        Qt6::Widgets
    )
    target_include_directories(tst_browsercontainer_isloading SYSTEM PRIVATE
        $<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>
    )
    if (WIN32)
        target_link_libraries(tst_browsercontainer_isloading PRIVATE delayimp.lib)
    endif()
    add_test(NAME tst_browsercontainer_isloading COMMAND tst_browsercontainer_isloading)
    set_tests_properties(tst_browsercontainer_isloading PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
