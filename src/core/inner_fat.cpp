// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <fmt/format.h>
#include "common/assert.h"
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
    auto header_iter = duplicate_data ? data.data() : partitionA.data();

    // Read header
    std::memcpy(&header, header_iter, sizeof(header));
    if (header.magic != MakeMagic('S', 'A', 'V', 'E') || header.version != 0x40000) {
        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    // Read filesystem information
    std::memcpy(&fs_info, header_iter + header.filesystem_information_offset, sizeof(fs_info));

    // Read data region
    if (duplicate_data) {
        data_region.resize(fs_info.data_region_block_count * fs_info.data_region_block_size);
        std::memcpy(data_region.data(), data.data() + fs_info.data_region_offset,
                    data_region.size());
    } else {
        data_region = std::move(partitionB);
    }

    // Directory & file entry tables are allocated in the data region as if they were normal
    // files. However, only continuous allocation has been observed so far according to 3DBrew,
    // so it should be safe to directly read the bytes.

    // Read directory entry table
    auto directory_entry_table_iter =
        header_iter + (duplicate_data ? fs_info.data_region_offset +
                                            fs_info.directory_entry_table.duplicate.block_index *
                                                fs_info.data_region_block_size
                                      : fs_info.directory_entry_table.non_duplicate);

    directory_entry_table.resize(fs_info.maximum_directory_count + 2); // including head and root
    std::memcpy(directory_entry_table.data(), directory_entry_table_iter,
                directory_entry_table.size() * sizeof(DirectoryEntryTableEntry));

    // Read file entry table
    auto file_entry_table_iter =
        header_iter + (duplicate_data ? fs_info.data_region_offset +
                                            fs_info.file_entry_table.duplicate.block_index *
                                                fs_info.data_region_block_size
                                      : fs_info.file_entry_table.non_duplicate);

    file_entry_table.resize(fs_info.maximum_file_count + 1); // including head
    std::memcpy(file_entry_table.data(), file_entry_table_iter,
                file_entry_table.size() * sizeof(FileEntryTableEntry));

    // Read file allocation table
    fat.resize(fs_info.file_allocation_table_entry_count);
    std::memcpy(fat.data(), header_iter + fs_info.file_allocation_table_offset,
                fat.size() * sizeof(FATNode));

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
    auto vsxe = vsxe_container.GetIVFCLevel4Data()[0];

    // Read header
    std::memcpy(&header, vsxe.data(), sizeof(header));
    if (header.magic != MakeMagic('V', 'S', 'X', 'E') || header.version != 0x30000) {
        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }

    // Read filesystem information
    std::memcpy(&fs_info, vsxe.data() + header.filesystem_information_offset, sizeof(fs_info));

    // Read data region
    data_region.resize(fs_info.data_region_block_count * fs_info.data_region_block_size);
    std::memcpy(data_region.data(), vsxe.data() + fs_info.data_region_offset, data_region.size());

    // Read directory entry table
    directory_entry_table.resize(fs_info.maximum_directory_count + 2); // including head and root
    std::memcpy(directory_entry_table.data(),
                vsxe.data() + fs_info.data_region_offset +
                    fs_info.directory_entry_table.duplicate.block_index *
                        fs_info.data_region_block_size,
                directory_entry_table.size() * sizeof(DirectoryEntryTableEntry));

    // Read file entry table
    file_entry_table.resize(fs_info.maximum_file_count + 1); // including head
    std::memcpy(file_entry_table.data(),
                vsxe.data() + fs_info.data_region_offset +
                    fs_info.file_entry_table.duplicate.block_index * fs_info.data_region_block_size,
                file_entry_table.size() * sizeof(FileEntryTableEntry));

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

    auto data = container.GetIVFCLevel4Data()[0];
    if (file.WriteBytes(data.data(), data.size()) != data.size()) {
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
