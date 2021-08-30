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
#include "core/file_sys/data/inner_fat.hpp"
#include "core/file_sys/ticket.h"

namespace Core {

struct TitleDBPreheader {
    u64_le db_magic;
    INSERT_PADDING_BYTES(0x78);
};
static_assert(sizeof(TitleDBPreheader) == 0x80, "TitleDB pre-header has incorrect size");

#pragma pack(push, 1)
struct TitleDBDirectoryEntryTableEntry {
    u32_le parent_directory_index;
    u32_le next_sibling_index;
    u32_le first_subdirectory_index;
    u32_le first_file_index;
    INSERT_PADDING_BYTES(12);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(TitleDBDirectoryEntryTableEntry) == 0x20,
              "TitleDBDirectoryEntryTableEntry has incorrect size");

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

class TitleDB;
using InnerFAT_TitleDB = InnerFAT<TitleDB, TitleDBPreheader, TitleDBDirectoryEntryTableEntry,
                                  TitleDBFileEntryTableEntry>;

class TitleDB final : public InnerFAT_TitleDB {
public:
    bool AddFromData(std::vector<u8> data);
    bool AddFromFile(const std::string& path);
    ~TitleDB();

    std::unordered_map<u64, TitleInfoEntry> titles;

private:
    bool Init(std::vector<u8> data);
    bool CheckMagic() const;
    bool LoadTitleInfo(u32 index);

    friend InnerFAT_TitleDB;
};

struct TicketDBPreheader {
    u32_le db_magic;
    INSERT_PADDING_BYTES(0x0C);
};
static_assert(sizeof(TicketDBPreheader) == 0x10, "TicketDB pre-header has incorrect size");

class TicketDB;
using InnerFAT_TicketDB = InnerFAT<TicketDB, TicketDBPreheader, TitleDBDirectoryEntryTableEntry,
                                   TitleDBFileEntryTableEntry>;
class TicketDB final : public InnerFAT_TicketDB {
public:
    bool AddFromFile(const std::string& path);
    ~TicketDB();

    std::unordered_map<u64, Ticket> tickets;

private:
    bool Init(std::vector<u8> data);
    bool CheckMagic() const;
    bool LoadTicket(u32 index);

    friend InnerFAT_TicketDB;
};

} // namespace Core
