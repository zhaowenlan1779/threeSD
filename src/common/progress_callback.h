// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "common/common_types.h"

namespace Common {

// (current_size, total_size)
using ProgressCallback = std::function<void(u64, u64)>;

class ProgressCallbackWrapper {
public:
    // (total imported size, total size)
    ProgressCallback Wrap(const ProgressCallback& callback);

    // (current content imported size, total imported size, total size)
    ProgressCallback Wrap(const std::function<void(u64, u64, u64)>& callback);

    void SetCurrent(u64 current);

    std::size_t total_size{};
    std::size_t current_done_size{};
    std::size_t current_pending_size{};
};

} // namespace Common
