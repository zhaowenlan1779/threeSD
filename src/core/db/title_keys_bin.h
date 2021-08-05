// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <unordered_map>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace Core {

struct TitleKeysBinHeader {
    u32_le num_entries;
    INSERT_PADDING_BYTES(12);
};
static_assert(sizeof(TitleKeysBinHeader) == 16);

struct TitleKeysBinEntry {
    u32_be common_key_index;
    INSERT_PADDING_BYTES(4);
    u64_be title_id;
    std::array<u8, 16> title_key;
};
static_assert(sizeof(TitleKeysBinEntry) == 32);

using TitleKeysMap = std::unordered_map<u64, TitleKeysBinEntry>;
class EncTitleKeysBin : public TitleKeysMap {};

// GM9 support files encTitleKeys.bin and decTitleKeys.bin.
bool LoadTitleKeysBin(TitleKeysMap& out, const std::string& path);

} // namespace Core
