// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/title_db.h"

namespace Core {

TitleDB::TitleDB(std::vector<u8> data) {
    is_good = Init(std::move(data));
}

bool TitleDB::IsGood() const {
    return is_good;
}

// Note: Title DB is actually a degenerate version of the Inner FAT.
// We are simplifying things as much as possible and not actually dealing with FAT nodes.
bool TitleDB::Init(std::vector<u8> data) {
    // Read header, FAT header and filesystem information
    TRY(CheckedMemcpy(&header, data, 0, sizeof(header)), LOG_ERROR(Core, "File size is too small"));

    if (header.db_magic != MakeMagic('N', 'A', 'N', 'D', 'T', 'D', 'B', 0) &&
        header.db_magic != MakeMagic('T', 'E', 'M', 'P', 'T', 'D', 'B', 0)) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    if (header.fat_header.magic != MakeMagic('B', 'D', 'R', 'I') ||
        header.fat_header.version != 0x30000) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    if (header.fat_header.image_block_size != 0x80 ||
        header.fs_info.data_region_block_size != 0x80) { // This simplifies things
        LOG_ERROR(Core, "Unexpected block size (this may be a bug)");
        return false;
    }

    // Read file entry table
    file_entry_table.resize(header.fs_info.maximum_file_count + 1); // including head

    auto file_entry_table_pos = TitleDBPreheaderSize + header.fs_info.data_region_offset +
                                header.fs_info.file_entry_table.duplicate.block_index *
                                    static_cast<std::size_t>(header.fs_info.data_region_block_size);

    TRY(CheckedMemcpy(file_entry_table.data(), data, file_entry_table_pos,
                      file_entry_table.size() * sizeof(TitleDBFileEntryTableEntry)),
        LOG_ERROR(Core, "File is too small"));

    // Read directory entry table for first file index
    auto first_file_index_pos =
        TitleDBPreheaderSize + header.fs_info.data_region_offset +
        header.fs_info.directory_entry_table.duplicate.block_index *
            static_cast<std::size_t>(header.fs_info.data_region_block_size) +
        0x20 /* sizeof TitleDB's directory entry (to skip head) */ +
        0x0C /* offset of first_file_index in directory entry of Title DB */;

    if (data.size() < first_file_index_pos + 4) {
        LOG_ERROR(Core, "File size is too small");
        return false;
    }

    const u32 first_file_index = *reinterpret_cast<u32_le*>(data.data() + first_file_index_pos);
    LOG_INFO(Core, "First file index is {}", first_file_index);
    u32 cur = first_file_index;
    while (cur != 0) {
        if (!LoadTitleInfo(data, cur)) {
            return false;
        }
        cur = file_entry_table[cur].next_sibling_index;
    }
    return true;
}

bool TitleDB::LoadTitleInfo(const std::vector<u8>& data, u32 index) {
    auto entry = file_entry_table[index];
    u32 block = entry.data_block_index;
    if (block == 0x80000000) { // empty file
        LOG_ERROR(Core, "Entry is an empty file");
        return false;
    }

    u64 file_size = entry.file_size;
    if (file_size != sizeof(TitleInfoEntry)) {
        LOG_ERROR(Core, "Entry has incorrect size {}", file_size);
        return false;
    }

    TitleInfoEntry title;
    const auto offset = TitleDBPreheaderSize + header.fs_info.data_region_offset +
                        block * header.fs_info.data_region_block_size;
    TRY(CheckedMemcpy(&title, data, offset, sizeof(TitleInfoEntry)),
        LOG_ERROR(Core, "File size is too small"));

    titles.emplace(entry.title_id, title);
    return true;
}

} // namespace Core
