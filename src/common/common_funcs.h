// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstring>
#include <string>
#include <type_traits>

#if !defined(ARCHITECTURE_x86_64)
#include <cstdlib> // for exit
#endif
#include "common/common_types.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/// Textually concatenates two tokens. The double-expansion is required by the C preprocessor.
#define CONCAT2(x, y) DO_CONCAT2(x, y)
#define DO_CONCAT2(x, y) x##y

#define TRY(x, ...)                                                                                \
    if (!(x)) {                                                                                    \
        __VA_ARGS__;                                                                               \
        return false;                                                                              \
    }

// helper macro to properly align structure members.
// Calling INSERT_PADDING_BYTES will add a new member variable with a name like "pad121",
// depending on the current source line to make sure variable names are unique.
#define INSERT_PADDING_BYTES(num_bytes) u8 CONCAT2(pad, __LINE__)[(num_bytes)]
#define INSERT_PADDING_WORDS(num_words) u32 CONCAT2(pad, __LINE__)[(num_words)]

// Inlining
#ifdef _WIN32
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

#ifndef _MSC_VER

#ifdef ARCHITECTURE_x86_64
#define Crash() __asm__ __volatile__("int $3")
#else
#define Crash() exit(1)
#endif

#else // _MSC_VER

#if (_MSC_VER < 1900)
// Function Cross-Compatibility
#define snprintf _snprintf
#endif

// Locale Cross-Compatibility
#define locale_t _locale_t

extern "C" {
__declspec(dllimport) void __stdcall DebugBreak(void);
}
#define Crash() DebugBreak()

#endif // _MSC_VER ndef

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
// Defined in Misc.cpp.
std::string GetLastErrorMsg();

template <typename T>
bool CheckedMemcpy(void* dest, T& container, std::ptrdiff_t offset, std::size_t size) {
    static_assert(std::is_same_v<typename T::value_type, u8>, "Only works with u8");
    if (container.size() < offset + size) {
        return false;
    }
    std::memcpy(dest, container.data() + offset, size);
    return true;
}

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

constexpr u64 MakeMagic(char a, char b, char c, char d, char e, char f, char g, char h) {
    return u64(a) | u64(b) << 8 | u64(c) << 16 | u64(d) << 24 | u64(e) << 32 | u64(f) << 40 |
           u64(g) << 48 | u64(h) << 56;
}

#define TRY_MEMCPY(dest, container, offset, size)                                                  \
    TRY(CheckedMemcpy(dest, container, offset, size), LOG_ERROR(Core, "File size is too small"))
