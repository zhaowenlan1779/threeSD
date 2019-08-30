# Enable modules to include each other's files
include_directories(.)

# CMake seems to only define _DEBUG on Windows
set_property(DIRECTORY APPEND PROPERTY
    COMPILE_DEFINITIONS $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)

# Ensure that projects build with Unicode support.
add_definitions(-DUNICODE -D_UNICODE)

# Set compilation flags
if (MSVC)
    set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "" FORCE)

    # Silence "deprecation" warnings
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -D_SCL_SECURE_NO_WARNINGS)

    # Avoid windows.h junk
    add_definitions(-DNOMINMAX)

    # Avoid windows.h from including some usually unused libs like winsocks.h, since this might cause some redefinition errors.
    add_definitions(-DWIN32_LEAN_AND_MEAN)

    # /W4                 - Level 4 warnings
    # /w34263             - Non-virtual member function hides base class virtual function
    # /w44265             - Class has virtual functions, but destructor is not virtual
    # /w34456             - Declaration of 'var' hides previous local declaration
    # /w34457             - Declaration of 'var' hides function parameter
    # /w34458             - Declaration of 'var' hides class member
    # /w34459             - Declaration of 'var' hides global definition
    # /w34946             - Reinterpret-cast between related types
    # /wd4592             - Symbol will be dynamically initialized (implementation limitation)
    # /MP                 - Multi-threaded compilation
    # /Zi                 - Output debugging information
    # /Zo                 - Enhanced debug info for optimized builds
    # /permissive-        - Enables stricter C++ standards conformance checks
    # /EHsc               - C++-only exception handling semantics
    # /volatile:iso       - Use strict standards-compliant volatile semantics.
    # /Zc:externConstexpr - Allow extern constexpr variables to have external linkage, like the standard mandates
    # /Zc:inline          - Let codegen omit inline functions in object files
    # /Zc:throwingNew     - Let codegen assume `operator new` (without std::nothrow) will never return null
    add_compile_options(
        /W4
        /w34263
        /w44265
        /w34456
        /w34457
        /w34458
        /w34459
        /w34946
        /wd4592
        /MP
        /Zi
        /Zo
        /permissive-
        /EHsc
        /std:c++latest
        /volatile:iso
        /Zc:externConstexpr
        /Zc:inline
        /Zc:throwingNew
    )

    # /GS- - No stack buffer overflow checks
    add_compile_options("$<$<CONFIG:Release>:/GS->")

    if (WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()

    set(CMAKE_EXE_LINKER_FLAGS_DEBUG   "/DEBUG /MANIFEST:NO" CACHE STRING "" FORCE)
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/DEBUG /MANIFEST:NO /INCREMENTAL:NO /OPT:REF,ICF" CACHE STRING "" FORCE)
else()
    add_compile_options(
        -Wall
        -Wno-attributes
        -pedantic
    )

    if (WARNINGS_AS_ERRORS)
        add_compile_options(-Werror -Wfatal-errors -pedantic-errors)
    endif()

    if (APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL Clang)
        add_compile_options("-stdlib=libc++")
    endif()

    # Set file offset size to 64 bits.
    #
    # On modern Unixes, this is typically already the case. The lone exception is
    # glibc, which may default to 32 bits. glibc allows this to be configured
    # by setting _FILE_OFFSET_BITS.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR MINGW)
        add_definitions(-D_FILE_OFFSET_BITS=64)
    endif()

    if (MINGW)
        add_definitions(-DMINGW_HAS_SECURE_API)
        if (COMPILE_WITH_DWARF)
            add_compile_options("-gdwarf")
        endif()

        if (MINGW_STATIC_BUILD)
            add_definitions(-DQT_STATICPLUGIN)
            add_compile_options("-static")
        endif()
    endif()

    if (NOT DEBUG)
        add_compile_options("-flto")
    endif()
endif()

add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(frontend)
