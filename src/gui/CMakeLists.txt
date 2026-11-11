set(_punto_qt_components Widgets)
if(UNIX AND NOT APPLE)
    list(APPEND _punto_qt_components DBus)
endif()
if(WIN32 OR APPLE)
    list(APPEND _punto_qt_components Network)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${_punto_qt_components})

qt_standard_project_setup()

qt_add_executable(rocket-switcher-config WIN32 MACOSX_BUNDLE
    main.cpp
    MainWindow.cpp
    HotkeyEditor.cpp
    DaemonClient.cpp
)
if(APPLE OR WIN32)
    target_sources(rocket-switcher-config PRIVATE SwitchEngine.cpp)
endif()
if(APPLE)
    target_sources(rocket-switcher-config PRIVATE MacSwitch.mm)
    set_source_files_properties(MacSwitch.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(WIN32)
    target_sources(rocket-switcher-config PRIVATE WinSwitch.cpp)
    target_link_libraries(rocket-switcher-config PRIVATE winmm)
endif()

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
if(APPLE)
    target_link_libraries(rocket-switcher-config PRIVATE
        "-framework Carbon"
        "-framework ApplicationServices"
        "-framework AppKit"
    )
endif()

if(BUILD_FCITX)
    target_compile_definitions(rocket-switcher-config PRIVATE PUNTO_WITH_FCITX=1)
else()
    target_compile_definitions(rocket-switcher-config PRIVATE PUNTO_WITH_FCITX=0)
endif()

target_compile_features(rocket-switcher-config PRIVATE cxx_std_17)

if(APPLE)
    set_target_properties(rocket-switcher-config PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Ракета"
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.rocket-switcher"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "rocket-switcher"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.app.in"
    )
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/rocket-switcher.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(rocket-switcher-config PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/rocket-switcher.icns")

    # Qt 6.8 still links -framework AGL; Xcode 26+ SDKs no longer ship it.
    # find_library can still see /System/Library/Frameworks/AGL.framework on
    # the live OS, but the linker uses CMAKE_OSX_SYSROOT and fails there.
    set(_agl_in_sdk "${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/AGL.framework")
    if(NOT EXISTS "${_agl_in_sdk}")
        set(_agl_stub_root "${CMAKE_BINARY_DIR}/stubs")
        set(_agl_fw "${_agl_stub_root}/AGL.framework")
        if(NOT EXISTS "${_agl_fw}/AGL")
            file(MAKE_DIRECTORY "${_agl_fw}")
            set(_agl_src "${CMAKE_BINARY_DIR}/agl_stub.c")
            file(WRITE "${_agl_src}" "void rocket_switcher_agl_stub(void) {}\n")
            set(_agl_isysroot)
            if(CMAKE_OSX_SYSROOT)
                list(APPEND _agl_isysroot -isysroot "${CMAKE_OSX_SYSROOT}")
            endif()
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" ${_agl_isysroot}
                    -x c -dynamiclib "${_agl_src}"
                    -o "${_agl_fw}/AGL"
                    -install_name "/System/Library/Frameworks/AGL.framework/AGL"
                COMMAND_ERROR_IS_FATAL ANY
            )
        endif()
        target_link_options(rocket-switcher-config PRIVATE "-F${_agl_stub_root}")
        message(STATUS "AGL.framework missing from SDK — linking stub ${_agl_fw}")
    endif()
endif()

# ---- Install ----
include(GNUInstallDirs)

install(TARGETS rocket-switcher-config
    BUNDLE DESTINATION .
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
