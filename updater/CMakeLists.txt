qt_add_executable(aerocore-browser-updater
    main.cpp
)

target_link_libraries(aerocore-browser-updater PRIVATE
    Qt6::Core
    Qt6::Network
)

target_compile_definitions(aerocore-browser-updater PRIVATE
    AEROCORE_BROWSER_VERSION="${PROJECT_VERSION}"
    $<$<NOT:$<CONFIG:Release>>:AEROCORE_ALLOW_INSECURE_HTTP=1>
)

set_target_properties(aerocore-browser-updater PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib"
)
