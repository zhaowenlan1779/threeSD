# CMake 3.12.4 required for 20 to be a valid value for CXX_STANDARD
cmake_minimum_required(VERSION 3.12.4)
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.15)
    # Add MSVC runtime library selection flags automatically:
    cmake_policy(SET CMP0091 OLD)
    # Don't override the warning flags in MSVC:
    cmake_policy(SET CMP0092 NEW)
endif ()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")
include(DownloadExternals)
include(CMakeDependentOption)

if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
endif()

project(threeSD)

option(WARNINGS_AS_ERRORS "Treat warnings as errors" ON)
CMAKE_DEPENDENT_OPTION(USE_BUNDLED_QT "Download bundled Qt binaries" ON "MSVC" OFF)
CMAKE_DEPENDENT_OPTION(COMPILE_WITH_DWARF "Add DWARF debugging information" ON "MINGW" OFF)

# Sanity check : Check that all submodules are present
# =======================================================================
function(check_submodules_present)
    file(READ "${PROJECT_SOURCE_DIR}/.gitmodules" gitmodules)
    string(REGEX MATCHALL "path *= *[^ \t\r\n]*" gitmodules ${gitmodules})
    foreach(module ${gitmodules})
        string(REGEX REPLACE "path *= *" "" module ${module})
        if (NOT EXISTS "${PROJECT_SOURCE_DIR}/${module}/.git")
            message(SEND_ERROR "Git submodule ${module} not found."
                    "Please run: git submodule update --init --recursive")
        endif()
    endforeach()
endfunction()
check_submodules_present()

# Detect current compilation architecture and create standard definitions
# =======================================================================

include(CheckSymbolExists)
function(detect_architecture symbol arch)
    if (NOT DEFINED ARCHITECTURE)
        set(CMAKE_REQUIRED_QUIET 1)
        check_symbol_exists("${symbol}" "" ARCHITECTURE_${arch})
        unset(CMAKE_REQUIRED_QUIET)

        # The output variable needs to be unique across invocations otherwise
        # CMake's crazy scope rules will keep it defined
        if (ARCHITECTURE_${arch})
            set(ARCHITECTURE "${arch}" PARENT_SCOPE)
            set(ARCHITECTURE_${arch} 1 PARENT_SCOPE)
            add_definitions(-DARCHITECTURE_${arch}=1)
        endif()
    endif()
endfunction()

if (NOT ENABLE_GENERIC)
    if (MSVC)
        detect_architecture("_M_AMD64" x86_64)
        detect_architecture("_M_IX86" x86)
        detect_architecture("_M_ARM" ARM)
        detect_architecture("_M_ARM64" ARM64)
    else()
        detect_architecture("__x86_64__" x86_64)
        detect_architecture("__i386__" x86)
        detect_architecture("__arm__" ARM)
        detect_architecture("__aarch64__" ARM64)
    endif()
endif()
if (NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "GENERIC")
    set(ARCHITECTURE_GENERIC 1)
    add_definitions(-DARCHITECTURE_GENERIC=1)
endif()
message(STATUS "Target architecture: ${ARCHITECTURE}")


# Configure C++ standard
# ===========================
if (MSVC)
    add_compile_options(/std:c++latest)
    add_definitions(-D_HAS_DEPRECATED_RESULT_OF)
else()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# set up output paths for executable binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

# System imported libraries
# ======================

# TODO: Is this necessary?

# Prefer the -pthread flag on Linux.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if (USE_BUNDLED_QT)
    if ((MSVC_VERSION GREATER_EQUAL 1910 AND MSVC_VERSION LESS 1930) AND ARCHITECTURE_x86_64)
        set(QT_VER qt-5.10.0-msvc2017_64)
    else()
        message(FATAL_ERROR "No bundled Qt binaries for your toolchain. Disable USE_BUNDLED_QT and provide your own.")
    endif()

    if (DEFINED QT_VER)
        download_bundled_external("qt/" ${QT_VER} QT_PREFIX)
    endif()

    set(QT_PREFIX_HINT HINTS "${QT_PREFIX}")
else()
    # Passing an empty HINTS seems to cause default system paths to get ignored in CMake 2.8 so
    # make sure to not pass anything if we don't have one.
    set(QT_PREFIX_HINT)
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets ${QT_PREFIX_HINT})

# Platform-specific library requirements
# ======================================
# TODO: Check the necessity of these

if (APPLE)
    # Umbrella framework for everything GUI-related
    find_library(COCOA_LIBRARY Cocoa)
    # For qdevicewatcher
    find_library(DISK_ARBITRATION_LIBRARY DiskArbitration)
    set(PLATFORM_LIBRARIES ${COCOA_LIBRARY} ${DISK_ARBITRATION_LIBRARY})
elseif (WIN32)
    # WSAPoll and SHGetKnownFolderPath (AppData/Roaming) didn't exist before WinNT 6.x (Vista)
    add_definitions(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600)
    set(PLATFORM_LIBRARIES winmm ws2_32)
    if (MINGW)
        # PSAPI is the Process Status API
        set(PLATFORM_LIBRARIES ${PLATFORM_LIBRARIES} psapi imm32 version)
    endif()
elseif (CMAKE_SYSTEM_NAME MATCHES "^(Linux|kFreeBSD|GNU|SunOS)$")
    set(PLATFORM_LIBRARIES rt)
endif()

# Include source code
# ===================
add_subdirectory(externals)
add_subdirectory(src)
