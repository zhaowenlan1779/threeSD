// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <share.h> // For _SH_DENYWR
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

#ifdef __WIN32
// _SH_DENYWR allows read-only access for other programs.
static FileUtil::IOFile g_log_file{FileUtil::GetExeDirectory() + DIR_SEP LOG_FILE, "w", _SH_DENYWR};
#elif __APPLE__
static FileUtil::IOFile g_log_file{FileUtil::GetBundleDirectory() + "/../" LOG_FILE, "w"};
#else
static FileUtil::IOFile g_log_file{ROOT_DIR DIR_SEP LOG_FILE, "w"};
#endif

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
