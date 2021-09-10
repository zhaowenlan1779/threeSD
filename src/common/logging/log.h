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

namespace Common::Logging {

std::uint64_t GetLoggingTime();

enum Level { Invalid = 0, Trace, Debug, Info, Warning, Error, Critical };
struct Entry {
    Level level;
    fmt::text_style style;
    std::string message;
};

void WriteLog(Entry entry);

// Returns up to 3 latest error messages
std::string GetLastErrors();

} // namespace Common::Logging

#define HELPER_STR(line) #line
#define HELPER_STR2(line) HELPER_STR(line)
#define LOG_PRINT(log_class, level, text_style, format_str, ...)                                   \
    Common::Logging::WriteLog(                                                                     \
        Common::Logging::Entry{Common::Logging::Level::level, text_style,                          \
                               fmt::format("[{:12.6f}] " log_class " <" #level "> " __FILE__       \
                                           ":" HELPER_STR2(__LINE__) ":{}: " format_str "\n",      \
                                           Common::Logging::GetLoggingTime() / 1000000.0,          \
                                           __func__ __VA_OPT__(, ) __VA_ARGS__)});

#ifdef _DEBUG
#define LOG_TRACE(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, Trace, fmt::fg(fmt::terminal_color::bright_black), __FILE__, __LINE__,   \
              __func__, __VA_ARGS__)
#else
#define LOG_TRACE(log_class, fmt, ...) (void(0))
#endif

#define LOG_DEBUG(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, Debug, fmt::fg(fmt::terminal_color::cyan), __VA_ARGS__)
#define LOG_INFO(log_class, ...)                                                                   \
    LOG_PRINT(#log_class, Info, fmt::fg(fmt::terminal_color::white), __VA_ARGS__)
#define LOG_WARNING(log_class, ...)                                                                \
    LOG_PRINT(#log_class, Warning, fmt::fg(fmt::terminal_color::bright_yellow), __VA_ARGS__)
#define LOG_ERROR(log_class, ...)                                                                  \
    LOG_PRINT(#log_class, Error, fmt::fg(fmt::terminal_color::bright_red), __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...)                                                               \
    LOG_PRINT(#log_class, Critical, fmt::fg(fmt::terminal_color::bright_magenta), __VA_ARGS__)
