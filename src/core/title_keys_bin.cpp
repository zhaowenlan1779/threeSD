// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "core/title_keys_bin.h"

namespace Core {

TitleKeysBin::TitleKeysBin(const std::string& path) {
    is_good = Load(path);
}

TitleKeysBin::~TitleKeysBin() = default;

bool TitleKeysBin::IsGood() const {
    return is_good;
}

bool TitleKeysBin::Load(const std::string& path) {
    FileUtil::IOFile file(path, "rb");
    if (!file) {
        LOG_ERROR(Core, "Could not open file {}", path);
        return false;
    }

    TitleKeysBinHeader header;
    if (file.ReadBytes(&header, sizeof(header)) != sizeof(header)) {
        LOG_ERROR(Core, "Could not read header from {}", path);
        return false;
    }

    for (std::size_t i = 0; i < header.num_entries; ++i) {
        TitleKeysBinEntry entry;
        if (file.ReadBytes(&entry, sizeof(entry)) != sizeof(entry)) {
            LOG_ERROR(Core, "Could not read entry {} from {}", i, path);
            return false;
        }
        entries.emplace(entry.title_id, entry);
    }

    if (file.Tell() != file.GetSize()) {
        LOG_ERROR(Core, "File {} has redundant data, may be corrupted");
        return false;
    }
    return true;
}

} // namespace Core
