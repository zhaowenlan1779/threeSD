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

// GM9 support files encTitleKeys.bin and decTitleKeys.bin.
class TitleKeysBin {
public:
    explicit TitleKeysBin(const std::string& path);
    ~TitleKeysBin();

    bool IsGood() const;

    std::unordered_map<u64, TitleKeysBinEntry> entries;

private:
    bool Load(const std::string& path);
    bool is_good = false;
};

} // namespace Core
