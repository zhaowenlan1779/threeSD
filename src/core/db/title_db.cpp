// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/db/title_db.h"
#include "core/file_sys/data/data_container.h"

namespace Core {

bool TitleDB::AddFromData(std::vector<u8> data) {
    return Init(std::move(data));
}

bool TitleDB::AddFromFile(const std::string& path) {
    FileUtil::IOFile file(path, "rb");
    DataContainer container(file.GetData());
    std::vector<std::vector<u8>> data;
    if (container.IsGood() && container.GetIVFCLevel4Data(data)) {
        return Init(std::move(data[0]));
    } else {
        return false;
    }
}

TitleDB::~TitleDB() = default;

bool TitleDB::Init(std::vector<u8> data) {
    if (!InnerFAT_TitleDB::Init({std::move(data)})) {
        return false;
    }

    u32 cur = directory_entry_table[1].first_file_index;
    while (cur != 0) {
        if (!LoadTitleInfo(cur)) {
            return false;
        }
        cur = file_entry_table[cur].next_sibling_index;
    }
    return true;
}

bool TitleDB::CheckMagic() const {
    if (header.pre_header.db_magic != MakeMagic('N', 'A', 'N', 'D', 'T', 'D', 'B', 0) &&
        header.pre_header.db_magic != MakeMagic('T', 'E', 'M', 'P', 'T', 'D', 'B', 0)) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    if (header.fat_header.magic != MakeMagic('B', 'D', 'R', 'I') ||
        header.fat_header.version != 0x30000) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }
    return true;
}

bool TitleDB::LoadTitleInfo(u32 index) {
    std::vector<u8> data;
    if (!GetFileData(data, index)) {
        return false;
    }

    TitleInfoEntry title;
    if (data.size() != sizeof(TitleInfoEntry)) {
        LOG_ERROR(Core, "Entry {} has incorrect size", index);
    }
    std::memcpy(&title, data.data(), data.size());

    titles.emplace(file_entry_table[index].title_id, title);
    return true;
}

bool TicketDB::AddFromFile(const std::string& path) {
    FileUtil::IOFile file(path, "rb");
    DataContainer container(file.GetData());
    std::vector<std::vector<u8>> data;
    if (container.IsGood() && container.GetIVFCLevel4Data(data)) {
        return Init(std::move(data[0]));
    } else {
        return false;
    }
}

TicketDB::~TicketDB() = default;

bool TicketDB::Init(std::vector<u8> data) {
    if (!InnerFAT_TicketDB::Init({std::move(data)})) {
        return false;
    }

    u32 cur = directory_entry_table[1].first_file_index;
    while (cur != 0) {
        if (!LoadTicket(cur)) {
            return false;
        }
        cur = file_entry_table[cur].next_sibling_index;
    }
    return true;
}

bool TicketDB::CheckMagic() const {
    if (header.pre_header.db_magic != MakeMagic('T', 'I', 'C', 'K')) {
        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    if (header.fat_header.magic != MakeMagic('B', 'D', 'R', 'I') ||
        header.fat_header.version != 0x30000) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }
    return true;
}

bool TicketDB::LoadTicket(u32 index) {
    std::vector<u8> data;
    if (!GetFileData(data, index)) {
        return false;
    }

    Ticket ticket;
    if (!ticket.Load(std::move(data), 8)) { // there is a 8-byte header
        return false;
    }
    tickets.emplace(file_entry_table[index].title_id, ticket);
    return true;
}

} // namespace Core
