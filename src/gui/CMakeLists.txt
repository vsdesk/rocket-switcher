set(_punto_qt_components Widgets)
if(UNIX AND NOT APPLE)
    list(APPEND _punto_qt_components DBus)
endif()
if(WIN32 OR APPLE)
    list(APPEND _punto_qt_components Network)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${_punto_qt_components})

qt_standard_project_setup()

qt_add_executable(rocket-switcher-config
    main.cpp
    MainWindow.cpp
    HotkeyEditor.cpp
    DaemonClient.cpp
)

target_link_libraries(rocket-switcher-config
    PRIVATE
        Qt6::Widgets
        PuntoSwitcher::Core
)
if(UNIX AND NOT APPLE)
    target_link_libraries(rocket-switcher-config PRIVATE Qt6::DBus)
endif()
if(WIN32 OR APPLE)
    target_link_libraries(rocket-switcher-config PRIVATE Qt6::Network)
endif()

if(BUILD_FCITX)
    target_compile_definitions(rocket-switcher-config PRIVATE PUNTO_WITH_FCITX=1)
else()
    target_compile_definitions(rocket-switcher-config PRIVATE PUNTO_WITH_FCITX=0)
endif()

target_compile_features(rocket-switcher-config PRIVATE cxx_std_17)

if(APPLE)
    set_target_properties(rocket-switcher-config PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Ракета"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.rocket-switcher"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

# ---- Install ----
include(GNUInstallDirs)

install(TARGETS rocket-switcher-config
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(UNIX AND NOT APPLE)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/rocket-switcher-config.desktop.in"
        "${CMAKE_CURRENT_BINARY_DIR}/rocket-switcher-config.desktop"
        @ONLY
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/rocket-switcher-config.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    )
endif()
