// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include "common/logging/log.h"
#include "common/string_util.h"

std::uint64_t GetLoggingTime() {
    static auto time_origin = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                 time_origin)
        .count();
}

std::string StandardizeLogClass(const std::string& log_class) {
    return Common::ReplaceAll(log_class, "_", ".");
}
