cmake_minimum_required(VERSION 3.20)

project(PuntoSwitcher
    VERSION 1.0.0
    DESCRIPTION "Ракета — ru↔en keyboard layout switcher (evdev daemon; optional Fcitx5)"
    HOMEPAGE_URL "https://github.com/vsdesk/rocket-switcher"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for IDEs/clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Hardening flags for Release builds (GCC on Linux).
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-O2 -D_FORTIFY_SOURCE=2 -fstack-protector-strong)
    add_link_options(-Wl,-z,relro,-z,now)
endif()

# Russian and English literals are UTF-8. MSVC does not assume that.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# Installation prefix defaults (for packaging)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# Fcitx5 cmake module search path — set by Fcitx5Core package
# Can be overridden: -DFCITX_INSTALL_CMAKECONFIG_DIR=...
find_package(ECM QUIET)  # KDE Extra CMake Modules (optional, for FindFcitx5)

# ---------------------------------------------------------------------------
# Submodules
# ---------------------------------------------------------------------------
option(BUILD_FCITX "Build Fcitx5 input method module (optional)" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(BUILD_DAEMON "Build standalone evdev/uinput daemon" ON)
else()
    option(BUILD_DAEMON "Build standalone evdev/uinput daemon" OFF)
endif()
option(BUILD_GUI "Build Qt settings (rocket-switcher-config)" ON)

add_subdirectory(src/core)

if(BUILD_FCITX)
    add_subdirectory(src/engine)
endif()

if(BUILD_GUI)
    add_subdirectory(src/gui)
endif()

if(BUILD_DAEMON)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_subdirectory(src/daemon)
    else()
        message(STATUS "rocket-switcher-daemon is Linux-only — skipping")
    endif()
endif()
# Note: src/daemon/CMakeLists.txt no-ops if libevdev/xkbcommon are unavailable.

# ---------------------------------------------------------------------------
# KDE/Fcitx5 migration helper (only relevant when using Fcitx module)
# ---------------------------------------------------------------------------
if(BUILD_FCITX)
    install(PROGRAMS scripts/rocket-switcher-setup
            DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---------------------------------------------------------------------------
# Tests (optional, enabled with -DBUILD_TESTING=ON)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT TARGET GTest::gtest)
        include(FetchContent)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
            FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            )
        else()
            FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
            )
        endif()
        FetchContent_MakeAvailable(googletest)
    endif()
    if(NOT TARGET GTest::gtest)
        message(FATAL_ERROR "GTest was not found and could not be downloaded")
    endif()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# CPack configuration (for reference — packaging is done via debhelper/rpmbuild)
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME              "rocket-switcher")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SHORT "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_CONTACT           "maintainer@example.com")
set(CPACK_PACKAGE_VENDOR            "Raketa")

install(FILES
    ${CMAKE_SOURCE_DIR}/data/dict/en.txt
    ${CMAKE_SOURCE_DIR}/data/dict/ru.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/rocket-switcher/dict)
install(FILES
    ${CMAKE_SOURCE_DIR}/data/sounds/switch.wav
    DESTINATION ${CMAKE_INSTALL_DATADIR}/rocket-switcher/sounds)

include(CPack)
