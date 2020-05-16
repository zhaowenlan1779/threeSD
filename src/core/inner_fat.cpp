// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "core/data_container.h"
#include "core/decryptor.h"
#include "core/inner_fat.h"

namespace Core {

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

InnerFAT::~InnerFAT() = default;

bool InnerFAT::IsGood() const {
    return is_good;
}

bool InnerFAT::ExtractDirectory(const std::string& path, std::size_t index) const {
    if (index >= directory_entry_table.size()) {
        LOG_ERROR(Core, "Index out of bound {}", index);
        return false;
    }
    auto entry = directory_entry_table[index];

    std::array<char, 17> name_data = {}; // Append a null terminator
    std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

    std::string name = name_data.data();
    std::string new_path = name.empty() ? path : path + name + "/"; // Name is empty for root

    if (!FileUtil::CreateFullPath(new_path)) {
        LOG_ERROR(Core, "Could not create path {}", new_path);
        return false;
    }

    // Files
    u32 cur = entry.first_file_index;
    while (cur != 0) {
        if (!ExtractFile(new_path, cur))
            return false;
        cur = file_entry_table[cur].next_sibling_index;
    }

    // Subdirectories
    cur = entry.first_subdirectory_index;
    while (cur != 0) {
        if (!ExtractDirectory(new_path, cur))
            return false;
        cur = directory_entry_table[cur].next_sibling_index;
    }

    return true;
}

bool InnerFAT::WriteMetadata(const std::string& path) const {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Core, "Could not create path {}", path);
        return false;
    }

    auto format_info = GetFormatInfo();

    FileUtil::IOFile file(path, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Core, "Could not open file {}", path);
        return false;
    }
    if (file.WriteBytes(&format_info, sizeof(format_info)) != sizeof(format_info)) {
        LOG_ERROR(Core, "Write data failed (file: {})", path);
        return false;
    }
    return true;
}

SDSavegame::SDSavegame(std::vector<std::vector<u8>> partitions) {
    if (partitions.size() == 1) {
        duplicate_data = true;
        data = std::move(partitions[0]);
    } else if (partitions.size() == 2) {
        duplicate_data = false;
        partitionA = std::move(partitions[0]);
        partitionB = std::move(partitions[1]);
    } else {
        UNREACHABLE();
    }
    is_good = Init();
}

SDSavegame::~SDSavegame() = default;

bool SDSavegame::Init() {
    const auto& header_vector = duplicate_data ? data : partitionA;

    // Read header
    TRY(CheckedMemcpy(&header, header_vector, 0, sizeof(header)),
        LOG_ERROR(Core, "File size is too small"));

    if (header.magic != MakeMagic('S', 'A', 'V', 'E') || header.version != 0x40000) {
        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    // Read filesystem information
    TRY(CheckedMemcpy(&fs_info, header_vector, header.filesystem_information_offset,
                      sizeof(fs_info)),
        LOG_ERROR(Core, "File size is too small"));

    // Read data region
    if (duplicate_data) {
        data_region.resize(fs_info.data_region_block_count * fs_info.data_region_block_size);
        TRY(CheckedMemcpy(data_region.data(), data, fs_info.data_region_offset, data_region.size()),
            LOG_ERROR(Core, "File size is too small"));
    } else {
        data_region = std::move(partitionB);
    }

    // Directory & file entry tables are allocated in the data region as if they were normal
    // files. However, only continuous allocation has been observed so far according to 3DBrew,
    // so it should be safe to directly read the bytes.

    // Read directory entry table
    directory_entry_table.resize(fs_info.maximum_directory_count + 2); // including head and root

    auto directory_entry_table_pos =
        duplicate_data
            ? fs_info.data_region_offset + fs_info.directory_entry_table.duplicate.block_index *
                                               fs_info.data_region_block_size
            : fs_info.directory_entry_table.non_duplicate;

    TRY(CheckedMemcpy(directory_entry_table.data(), header_vector, directory_entry_table_pos,
                      directory_entry_table.size() * sizeof(DirectoryEntryTableEntry)),
        LOG_ERROR(Core, "File is too small"));

    // Read file entry table
    file_entry_table.resize(fs_info.maximum_file_count + 1); // including head

    auto file_entry_table_pos =
        duplicate_data
            ? fs_info.data_region_offset +
                  fs_info.file_entry_table.duplicate.block_index * fs_info.data_region_block_size
            : fs_info.file_entry_table.non_duplicate;

    TRY(CheckedMemcpy(file_entry_table.data(), header_vector, file_entry_table_pos,
                      file_entry_table.size() * sizeof(FileEntryTableEntry)),
        LOG_ERROR(Core, "File is too small"));

    // Read file allocation table
    fat.resize(fs_info.file_allocation_table_entry_count);
    TRY(CheckedMemcpy(fat.data(), header_vector, fs_info.file_allocation_table_offset,
                      fat.size() * sizeof(FATNode)),
        LOG_ERROR(Core, "File size is too small"));

    return true;
}

bool SDSavegame::ExtractFile(const std::string& path, std::size_t index) const {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Core, "Could not create path {}", path);
        return false;
    }

    if (index >= file_entry_table.size()) {
        LOG_ERROR(Core, "Index out of bound {}", index);
        return false;
    }
    auto entry = file_entry_table[index];

    std::array<char, 17> name_data = {}; // Append a null terminator
    std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

    std::string name = name_data.data();
    FileUtil::IOFile file(path + name, "wb");
    if (!file) {
        LOG_ERROR(Core, "Could not open file {}", path + name);
        return false;
    }

    u32 block = entry.data_block_index;
    if (block == 0x80000000) { // empty file
        return true;
    }

    u64 file_size = entry.file_size;
    while (true) {
        // Entry index is block index + 1
        auto block_data = fat[block + 1];

        u32 last_block = block;
        if (block_data.v.flag) { // This node has multiple entries
            last_block = fat[block + 2].v.index - 1;
        }

        const std::size_t size = fs_info.data_region_block_size * (last_block - block + 1);
        const std::size_t to_write = std::min(file_size, size);

        if (data_region.size() < fs_info.data_region_block_size * block + to_write) {
            LOG_ERROR(Core, "Out of bound block: {} to_write: {}", block, to_write);
            return false;
        }
        if (file.WriteBytes(data_region.data() + fs_info.data_region_block_size * block,
                            to_write) != to_write) {
            LOG_ERROR(Core, "Write data failed (file: {})", path + name);
            return false;
        }
        file_size -= to_write;

        if (block_data.v.index == 0 || file_size == 0) // last node
            break;

        block = block_data.v.index - 1;
    }

    return true;
}

bool SDSavegame::Extract(std::string path) const {
    if (path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    // All saves on a physical 3DS are called 00000001.sav
    if (!ExtractDirectory(path + "00000001/", 1)) { // Directory 1 = root
        return false;
    }

    if (!WriteMetadata(path + "00000001.metadata")) {
        return false;
    }

    return true;
}

ArchiveFormatInfo SDSavegame::GetFormatInfo() const {
    // Tests on a physical 3DS shows that the `total_size` field seems to always be 0
    // when requested with the UserSaveData archive, and 134328448 when requested with
    // the SaveData archive. More investigation is required to tell whether this is a fixed value.
    ArchiveFormatInfo format_info = {/* total_size */ 0x40000,
                                     /* number_directories */ fs_info.maximum_directory_count,
                                     /* number_files */ fs_info.maximum_file_count,
                                     /* duplicate_data */ duplicate_data};

    return format_info;
}

SDExtdata::SDExtdata(std::string data_path_, const SDMCDecryptor& decryptor_)
    : data_path(std::move(data_path_)), decryptor(decryptor_) {

    if (data_path.back() != '/' && data_path.back() != '\\') {
        data_path += '/';
    }

    is_good = Init();
}

SDExtdata::~SDExtdata() = default;

bool SDExtdata::Init() {
    // Read VSXE file
    auto vsxe_raw = decryptor.DecryptFile(data_path + "00000000/00000001");
    if (vsxe_raw.empty()) {
        LOG_ERROR(Core, "Failed to load or decrypt VSXE");
        return false;
    }
    DataContainer vsxe_container(std::move(vsxe_raw));
    if (!vsxe_container.IsGood()) {
        return false;
    }

    std::vector<std::vector<u8>> container_data;
    if (!vsxe_container.GetIVFCLevel4Data(container_data)) {
        return false;
    }
    const auto& vsxe = container_data[0];

    // Read header
    TRY(CheckedMemcpy(&header, vsxe, 0, sizeof(header)), LOG_ERROR(Core, "File size is too small"));

    if (header.magic != MakeMagic('V', 'S', 'X', 'E') || header.version != 0x30000) {
        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    // Read filesystem information
    TRY(CheckedMemcpy(&fs_info, vsxe, header.filesystem_information_offset, sizeof(fs_info)),
        LOG_ERROR(Core, "File size is too small"));

    // Read data region
    TRY(CheckedMemcpy(data_region.data(), vsxe, fs_info.data_region_offset, data_region.size()),
        LOG_ERROR(Core, "File size is too small"));

    // Read directory entry table
    directory_entry_table.resize(fs_info.maximum_directory_count + 2); // including head and root

    const auto directory_entry_table_pos =
        fs_info.data_region_offset +
        fs_info.directory_entry_table.duplicate.block_index * fs_info.data_region_block_size;

    TRY(CheckedMemcpy(directory_entry_table.data(), vsxe, directory_entry_table_pos,
                      directory_entry_table.size() * sizeof(DirectoryEntryTableEntry)),
        LOG_ERROR(Core, "File size is too small"));

    // Read file entry table
    file_entry_table.resize(fs_info.maximum_file_count + 1); // including head

    const auto file_entry_table_pos =
        fs_info.data_region_offset +
        fs_info.file_entry_table.duplicate.block_index * fs_info.data_region_block_size;

    TRY(CheckedMemcpy(file_entry_table.data(), vsxe, file_entry_table_pos,
                      file_entry_table.size() * sizeof(FileEntryTableEntry)),
        LOG_ERROR(Core, "File size is too small"));

    // File allocation table isn't needed here, as the only files allocated by them are
    // directory/file entry tables which we already read above.
    return true;
}

bool SDExtdata::Extract(std::string path) const {
    if (path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    if (!ExtractDirectory(path, 1)) {
        return false;
    }

    if (!WriteMetadata(path + "metadata")) {
        return false;
    }

    return true;
}

bool SDExtdata::ExtractFile(const std::string& path, std::size_t index) const {
    /// Maximum amount of device files a device directory can hold.
    constexpr u32 DeviceDirCapacity = 126;

    if (index >= file_entry_table.size()) {
        LOG_ERROR(Core, "Index out of bound {}", index);
        return false;
    }
    auto entry = file_entry_table[index];

    std::array<char, 17> name_data = {}; // Append a null terminator
    std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

    std::string name = name_data.data();
    FileUtil::IOFile file(path + name, "wb");
    if (!file) {
        LOG_ERROR(Core, "Could not open file {}", path + name);
        return false;
    }

    u32 file_index = index + 1;
    u32 sub_directory_id = file_index / DeviceDirCapacity;
    u32 sub_file_id = file_index % DeviceDirCapacity;
    std::string device_file_path =
        fmt::format("{}{:08x}/{:08x}", data_path, sub_directory_id, sub_file_id);

    auto container_data = decryptor.DecryptFile(device_file_path);
    if (container_data.empty()) { // File does not exist?
        LOG_WARNING(Core, "Ignoring file {}", device_file_path);
        return true;
    }

    DataContainer container(std::move(container_data));
    if (!container.IsGood()) {
        return false;
    }

    std::vector<std::vector<u8>> data;
    if (!container.GetIVFCLevel4Data(data)) {
        return false;
    }

    if (file.WriteBytes(data[0].data(), data[0].size()) != data[0].size()) {
        LOG_ERROR(Core, "Write data failed (file: {})", path + name);
        return false;
    }

    return true;
}

ArchiveFormatInfo SDExtdata::GetFormatInfo() const {
    // This information is based on how Citra created the metadata in FS
    ArchiveFormatInfo format_info = {/* total_size */ 0,
                                     /* number_directories */ fs_info.maximum_directory_count,
                                     /* number_files */ fs_info.maximum_file_count,
                                     /* duplicate_data */ false};

    return format_info;
}

} // namespace Core
