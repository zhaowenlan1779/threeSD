// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstddef>
#include <cstring>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#endif

#include "common/common_funcs.h"

// Generic function to get last error message.
// Call directly after the command or use the error num.
// This function might change the error code.
std::string GetLastErrorMsg() {
    static const std::size_t buff_size = 255;
    char err_str[buff_size];

#if defined(_WIN32)
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, GetLastError(),
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), err_str, buff_size, nullptr);
#else
    auto ret = strerror_r(errno, err_str, buff_size);
    if constexpr (std::is_same_v<decltype(ret), const char*>) {
        // GNU specific
        // This is a workaround for XSI-compliant variant; this should always be safe.
        const char* str = reinterpret_cast<const char*>(ret);
        return std::string(str, strlen(str));
    }
#endif

    return std::string(err_str, strnlen(err_str, buff_size));
}
