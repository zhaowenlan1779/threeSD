// Copyright 2014 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// This is simplified version of Citra's logging system.
// Only stderr/stderr output is enabled and color is implemented
// with fmt tools.

#pragma once

#include <cstdio>
#include <fmt/color.h>
#include <fmt/format.h>

std::uint64_t GetLoggingTime();
std::string StandardizeLogClass(const std::string& log_class);

// TODO: Use a standard variant of ##__VA_ARGS__?
#define LOG_PRINT(log_class, level, text_style, file, line, func, format, ...)                     \
    {                                                                                              \
        fmt::print(stderr, text_style, "[{:12.6f}] {} <{}> {}:{}:{}: " format "\n",                \
                   GetLoggingTime() / 1000000.0, StandardizeLogClass(log_class), level, file,      \
                   line, func, ##__VA_ARGS__);                                                     \
        fflush(stderr);                                                                            \
    }

#ifdef _DEBUG
#define LOG_TRACE(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, "Trace", fmt::fg(fmt::terminal_color::bright_black), __FILE__, __LINE__, \
              __func__, __VA_ARGS__)
#else
#define LOG_TRACE(log_class, fmt, ...) (void(0))
#endif

#define LOG_DEBUG(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, "Debug", fmt::fg(fmt::terminal_color::cyan), __FILE__, __LINE__,         \
              __func__, __VA_ARGS__)
#define LOG_INFO(log_class, ...)                                                                   \
    LOG_PRINT(#log_class, "Info", fmt::fg(fmt::terminal_color::white), __FILE__, __LINE__,         \
              __func__, __VA_ARGS__)
#define LOG_WARNING(log_class, ...)                                                                \
    LOG_PRINT(#log_class, "Warning", fmt::fg(fmt::terminal_color::bright_yellow), __FILE__,        \
              __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, "Error", fmt::fg(fmt::terminal_color::bright_red), __FILE__, __LINE__,   \
              __func__, __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...)                                                               \
    LOG_PRINT(#log_class, "Critical", fmt::fg(fmt::terminal_color::bright_magenta), __FILE__,      \
              __LINE__, __func__, __VA_ARGS__)
