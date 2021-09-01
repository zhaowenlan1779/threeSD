// Copyright 2014 Citra Emulator Project / 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/swap.h"

namespace Core {

constexpr u32 ConfigSavegameSize = 0x8000;
/// The maximum number of block entries that can exist in the config file
constexpr u32 ConfigSavegameMaxEntries = 1479;

/// Block header in the config savedata file
struct ConfigSavegameBlockEntry {
    u32_le block_id;       ///< The id of the current block
    u32_le offset_or_data; ///< This is the absolute offset to the block data if the size is greater
                           /// than 4 bytes, otherwise it contains the data itself
    u16_le size;           ///< The size of the block
    u16_le flags;          ///< The flags of the block, possibly used for access control
};

struct ConfigSavegameHeader {
    u16_le total_entries;       ///< The total number of set entries in the config file
    u16_le data_entries_offset; ///< The offset where the data for the blocks start
    /// The block headers, the maximum possible value is 1479 as per hardware
    std::array<ConfigSavegameBlockEntry, ConfigSavegameMaxEntries> block_entries;
    u32_le unknown; ///< This field is unknown, possibly padding, 0 has been observed in hardware
};
static_assert(sizeof(ConfigSavegameHeader) == 0x455C,
              "Config Savegame header must be exactly 0x455C bytes");

// This is not the config savegame itself, but rather the `config` file inside.
class ConfigSavegame {
public:
    explicit ConfigSavegame();
    ~ConfigSavegame();

    bool Init(std::vector<u8> data);
    u8 GetSystemLanguage() const;

private:
    ConfigSavegameHeader header;
};

} // namespace Core
