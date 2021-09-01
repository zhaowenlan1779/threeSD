// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstring>
#include "common/logging/log.h"
#include "core/file_sys/config_savegame.h"

namespace Core {

ConfigSavegame::ConfigSavegame() = default;
ConfigSavegame::~ConfigSavegame() = default;

bool ConfigSavegame::Init(std::vector<u8> data) {
    if (data.size() != ConfigSavegameSize) {
        LOG_ERROR(Core, "Config savegame is of incorrect size");
        return false;
    }
    std::memcpy(&header, data.data(), sizeof(header));
    return true;
}

u8 ConfigSavegame::GetSystemLanguage() const {
    static constexpr u32 LanguageBlockID = 0x000A0002;

    const auto iter = std::find_if(
        header.block_entries.begin(), header.block_entries.end(),
        [](const ConfigSavegameBlockEntry& entry) { return entry.block_id == LanguageBlockID; });
    if (iter == header.block_entries.end()) {
        LOG_ERROR(Core, "Cannot find Language config block, returning default (English)");
        return 1;
    }
    return static_cast<u8>(iter->offset_or_data);
}

} // namespace Core
