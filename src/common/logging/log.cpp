// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <share.h> // For _SH_DENYWR
#else
#define _SH_DENYWR 0
#endif

#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"

namespace Common::Logging {

std::uint64_t GetLoggingTime() {
    static auto time_origin = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                 time_origin)
        .count();
}

// _SH_DENYWR allows read-only access for other programs. For non-Windows it's defined to 0.
static FileUtil::IOFile g_log_file{LOG_FILE, "w", _SH_DENYWR};

static std::array<Entry, 3> g_error_buffer{};
static int g_error_buffer_pos = 0;

void WriteLog(Entry entry) {
    // stderr
    fmt::print(stderr, entry.style, entry.message);

    // log file
    g_log_file.WriteString(entry.message);
    if (entry.level >= Level::Error) {
        g_log_file.Flush(); // Do not flush the file too often
    }

    // log buffer
    if (entry.level >= Level::Error) {
        g_error_buffer[g_error_buffer_pos] = std::move(entry);
        g_error_buffer_pos = (g_error_buffer_pos + 1) % g_error_buffer.size();
    }
}

std::string GetLastErrors() {
    std::string output;
    for (std::size_t i = 0; i < g_error_buffer.size(); ++i) {
        const std::size_t pos = (g_error_buffer_pos + i) % g_error_buffer.size();
        if (g_error_buffer[pos].level != Level::Invalid) {
            output.append(g_error_buffer[pos].message);
        }
    }
    return output;
}

} // namespace Common::Logging
