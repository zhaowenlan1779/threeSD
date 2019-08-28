// Copyright 2014 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// This is simplified version of Citra's logging system.
// Only stderr/stderr output is enabled and color is implemented
// for the UNIX-like.

#pragma once

#include <chrono>
#include <cstdio>
#include <string>
#include <fmt/format.h>
#include "common/string_util.h"

#include <iostream>

template <typename... Args>
void PrintLog(std::FILE* f, const std::string& log_class, const std::string& level,
              const std::string& color, const std::string& file, int line, const std::string& func,
              const std::string& format, Args&&... args) {
    static auto time_origin = std::chrono::steady_clock::now();
    const u64 us = std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - time_origin)
                       .count();
    const auto real_class = Common::ReplaceAll(log_class, "_", ".");
    try {
        fmt::print(f, "\x1b{}[{:12.6f}] {} <{}> {}:{}:{}: " + format + "\x1b[0m\n", color,
                   us / 1000000.0, real_class, level, file, line, func, args...);
        fflush(stderr);
    } catch (...) {
        std::cerr << "(unexpected) fmt failed with exception" << std::endl;
    }
}

#ifdef _DEBUG
#define LOG_TRACE(log_class, ...)                                                                  \
    PrintLog(stderr, #log_class, "Trace", "[1;30m", __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define LOG_TRACE(log_class, fmt, ...) (void(0))
#endif

#define LOG_DEBUG(log_class, ...)                                                                  \
    PrintLog(stderr, #log_class, "Debug", "[0;36m", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_INFO(log_class, ...)                                                                   \
    PrintLog(stderr, #log_class, "Info", "[0;37m", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_WARNING(log_class, ...)                                                                \
    PrintLog(stderr, #log_class, "Warning", "[1;33m", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_ERROR(log_class, ...)                                                                  \
    PrintLog(stderr, #log_class, "Error", "[1;31m", __FILE__, __LINE__, __func__, __VA_ARGS__)
#define LOG_CRITICAL(log_class, ...)                                                               \
    PrintLog(stderr, #log_class, "Critical", "[1;35m", __FILE__, __LINE__, __func__, __VA_ARGS__)
