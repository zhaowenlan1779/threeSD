// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/progress_callback.h"

namespace Common {

ProgressCallback ProgressCallbackWrapper::Wrap(const ProgressCallback& callback) {
    current_done_size += current_pending_size; // Last content finished
    return [this, callback](std::size_t current, std::size_t total) {
        current_pending_size = total;
        callback(current + current_done_size, total_size);
    };
}

ProgressCallback ProgressCallbackWrapper::Wrap(
    const std::function<void(std::size_t, std::size_t, std::size_t)>& callback) {

    current_done_size += current_pending_size; // Last content finished
    return [this, callback](std::size_t current, std::size_t total) {
        current_pending_size = total;
        callback(current, current + current_done_size, total_size);
    };
}

void ProgressCallbackWrapper::SetCurrent(u64 current) {
    current_done_size = current;
    current_pending_size = 0;
}

} // namespace Common
