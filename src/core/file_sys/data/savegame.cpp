// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/data/savegame.h"

namespace Core {

Savegame::Savegame(std::vector<std::vector<u8>> partitions) {
    is_good = Archive<Savegame>::Init(std::move(partitions));
}

Savegame::~Savegame() = default;

bool Savegame::CheckMagic() const {
    if (header.fat_header.magic != MakeMagic('S', 'A', 'V', 'E') ||
        header.fat_header.version != 0x40000) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }
    return true;
}

bool Savegame::IsGood() const {
    return is_good;
}

bool Savegame::ExtractFile(const std::string& path, std::size_t index) const {
    std::vector<u8> data;
    if (!GetFileData(data, index)) {
        LOG_ERROR(Core, "Could not get file data for index {}", index);
        return false;
    }
    return FileUtil::WriteBytesToFile(path, data.data(), data.size());
}

bool Savegame::Extract(std::string path) const {
    if (path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    // All saves on a physical 3DS are called 00000001.sav
    if (!ExtractDirectory(path + "00000001/", 1)) { // Directory 1 = root
        return false;
    }

    // Write format info
    const auto format_info = GetFormatInfo();
    return FileUtil::WriteBytesToFile(path + "00000001.metadata", &format_info,
                                      sizeof(format_info));
}

ArchiveFormatInfo Savegame::GetFormatInfo() const {
    // Tests on a physical 3DS shows that the `total_size` field seems to always be 0
    // when requested with the UserSaveData archive, and 134328448 when requested with
    // the SaveData archive. More investigation is required to tell whether this is a fixed value.
    ArchiveFormatInfo format_info = {/* total_size */ 0x40000,
                                     /* number_directories */ fs_info.maximum_directory_count,
                                     /* number_files */ fs_info.maximum_file_count,
                                     /* duplicate_data */ duplicate_data};

    return format_info;
}

} // namespace Core
