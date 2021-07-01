// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <unordered_map>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/inner_fat.h"

namespace Core {

struct TitleDBHeader {
    u64_le db_magic;
    INSERT_PADDING_BYTES(0x78);
    FATHeader fat_header;
    FileSystemInformation fs_info;
};
constexpr std::size_t TitleDBPreheaderSize = 0x80;
static_assert(sizeof(TitleDBHeader) ==
                  TitleDBPreheaderSize + sizeof(FATHeader) + sizeof(FileSystemInformation),
              "TitleDB preheader has incorrect size");

#pragma pack(push, 1)
struct TitleDBFileEntryTableEntry {
    u32_le parent_directory_index;
    u64_le title_id;
    u32_le next_sibling_index;
    INSERT_PADDING_BYTES(4);
    u32_le data_block_index;
    u64_le file_size;
    INSERT_PADDING_BYTES(8);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(TitleDBFileEntryTableEntry) == 0x2c,
              "TitleDBFileEntryTableEntry has incorrect size");
#pragma pack(pop)

struct TitleInfoEntry {
    u64_le title_size;
    u32_le title_type;
    u32_le title_version;
    u32_le flags0;
    u32_le tmd_content_id;
    u32_le cmd_content_id;
    u32_le flags1;
    u32_le extdata_id_low;
    INSERT_PADDING_BYTES(4);
    u64_le flags2;
    std::array<u8, 0x10> product_code;
    INSERT_PADDING_BYTES(0x40);
};
static_assert(sizeof(TitleInfoEntry) == 0x80, "TitleInfoEntry has incorrect size");

class TitleDB {
public:
    explicit TitleDB(std::vector<u8> data);
    bool IsGood() const;

    std::unordered_map<u64, TitleInfoEntry> titles;

private:
    bool Init(std::vector<u8> data);
    bool LoadTitleInfo(const std::vector<u8>& data, u32 index);

    bool is_good = false;
    TitleDBHeader header;
    std::vector<TitleDBFileEntryTableEntry> file_entry_table;
};

} // namespace Core
