// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>

namespace Common {

// (current_size, total_size)
using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

} // namespace Common
