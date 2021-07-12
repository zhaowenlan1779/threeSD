// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <type_traits>
#include <vector>
#include "common/assert.h"
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/swap.h"

namespace Core {

union TableOffset {
    // This has different meanings for different savegame layouts
    struct { // duplicate data = true
        u32_le block_index;
        u32_le block_count;
    } duplicate;

    u64_le non_duplicate; // duplicate data = false
};

struct FATHeader {
    u32_le magic;
    u32_le version;
    u64_le filesystem_information_offset;
    u64_le image_size;
    u32_le image_block_size;
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(FATHeader) == 0x20, "FATHeader has incorrect size");

struct FileSystemInformation {
    INSERT_PADDING_BYTES(4);
    u32_le data_region_block_size;
    u64_le directory_hash_table_offset;
    u32_le directory_hash_table_bucket_count;
    INSERT_PADDING_BYTES(4);
    u64_le file_hash_table_offset;
    u32_le file_hash_table_bucket_count;
    INSERT_PADDING_BYTES(4);
    u64_le file_allocation_table_offset;
    u32_le file_allocation_table_entry_count;
    INSERT_PADDING_BYTES(4);
    u64_le data_region_offset;
    u32_le data_region_block_count;
    INSERT_PADDING_BYTES(4);
    TableOffset directory_entry_table;
    u32_le maximum_directory_count;
    INSERT_PADDING_BYTES(4);
    TableOffset file_entry_table;
    u32_le maximum_file_count;
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(FileSystemInformation) == 0x68, "FileSystemInformation has incorrect size");

struct DirectoryEntryTableEntry {
    u32_le parent_directory_index;
    std::array<char, 16> name;
    u32_le next_sibling_index;
    u32_le first_subdirectory_index;
    u32_le first_file_index;
    INSERT_PADDING_BYTES(4);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(DirectoryEntryTableEntry) == 0x28,
              "DirectoryEntryTableEntry has incorrect size");

struct FileEntryTableEntry {
    u32_le parent_directory_index;
    std::array<char, 16> name;
    u32_le next_sibling_index;
    INSERT_PADDING_BYTES(4);
    u32_le data_block_index;
    u64_le file_size;
    INSERT_PADDING_BYTES(4);
    u32_le next_hash_bucket_entry;
};
static_assert(sizeof(FileEntryTableEntry) == 0x30, "FileEntryTableEntry has incorrect size");

struct FATNode {
    union {
        BitField<0, 31, u32> index;
        BitField<31, 1, u32> flag;

        u32_le raw;
    } u, v;
};

namespace detail {

#pragma pack(push, 1)
template <typename Preheader>
struct FullHeaderInternal {
    static constexpr std::size_t PreheaderSize = sizeof(Preheader);
    Preheader pre_header;
    FATHeader fat_header;
};

template <>
struct FullHeaderInternal<void> {
    static constexpr std::size_t PreheaderSize = 0;
    FATHeader fat_header;
};
#pragma pack(pop)

template <typename Preheader>
struct FullHeaderInternal2 {
    using Type = FullHeaderInternal<Preheader>;

    static_assert(sizeof(Type) == sizeof(Preheader) + sizeof(FATHeader));
    static_assert(std::is_standard_layout_v<Type>);
};

template <>
struct FullHeaderInternal2<void> {
    using Type = FullHeaderInternal<void>;

    static_assert(sizeof(Type) == sizeof(FATHeader));
    static_assert(std::is_standard_layout_v<Type>);
};

} // namespace detail

template <typename Preheader = void>
using FullHeader = typename detail::FullHeaderInternal2<Preheader>::Type;

template <typename T, typename Preheader = void,
          typename DirectoryEntryType = DirectoryEntryTableEntry,
          typename FileEntryType = FileEntryTableEntry>
class InnerFAT {
protected:
    bool Init(std::vector<std::vector<u8>> partitions) {
        duplicate_data = partitions.size() == 1;
        const auto& header_vector = partitions[0];

        // Read header
        TRY(CheckedMemcpy(&header, header_vector, 0, sizeof(header)),
            LOG_ERROR(Core, "File size is too small"));

        if (!static_cast<const T*>(this)->CheckMagic()) {
            LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
            return false;
        }

        static constexpr std::size_t PreheaderSize = FullHeader<Preheader>::PreheaderSize;

        // Read filesystem information
        TRY(CheckedMemcpy(&fs_info, header_vector,
                          PreheaderSize + header.fat_header.filesystem_information_offset,
                          sizeof(fs_info)),
            LOG_ERROR(Core, "File size is too small"));

        // Read data region
        if (duplicate_data) {
            data_region.resize(fs_info.data_region_block_count *
                               static_cast<std::size_t>(fs_info.data_region_block_size));
            // This check is relaxed (not counting in PreheaderSize) for title.db
            if (partitions[0].size() < fs_info.data_region_offset + data_region.size()) {
                LOG_ERROR(Core, "File size is too small");
                return false;
            }
            const auto offset = PreheaderSize + fs_info.data_region_offset;
            ASSERT(partitions[0].size() > offset);
            const auto to_copy =
                std::min<std::size_t>(data_region.size(), partitions[0].size() - offset);
            std::memcpy(data_region.data(), partitions[0].data() + offset, to_copy);
        } else {
            data_region = std::move(partitions[1]);
        }

        // Directory & file entry tables are allocated in the data region as if they were normal
        // files. However, only continuous allocation has been observed so far according to 3DBrew,
        // so it should be safe to directly read the bytes.

        // Read directory entry table, +2 to include head and root
        directory_entry_table.resize(fs_info.maximum_directory_count + 2);

        auto directory_entry_table_pos =
            duplicate_data ? PreheaderSize + fs_info.data_region_offset +
                                 fs_info.directory_entry_table.duplicate.block_index *
                                     static_cast<std::size_t>(fs_info.data_region_block_size)
                           : PreheaderSize + fs_info.directory_entry_table.non_duplicate;

        TRY(CheckedMemcpy(directory_entry_table.data(), header_vector, directory_entry_table_pos,
                          directory_entry_table.size() * sizeof(DirectoryEntryType)),
            LOG_ERROR(Core, "File is too small"));

        // Read file entry table
        file_entry_table.resize(fs_info.maximum_file_count + 1); // including head

        auto file_entry_table_pos =
            duplicate_data ? PreheaderSize + fs_info.data_region_offset +
                                 fs_info.file_entry_table.duplicate.block_index *
                                     static_cast<std::size_t>(fs_info.data_region_block_size)
                           : PreheaderSize + fs_info.file_entry_table.non_duplicate;

        TRY(CheckedMemcpy(file_entry_table.data(), header_vector, file_entry_table_pos,
                          file_entry_table.size() * sizeof(FileEntryType)),
            LOG_ERROR(Core, "File is too small"));

        // Read file allocation table
        fat.resize(fs_info.file_allocation_table_entry_count);
        TRY(CheckedMemcpy(fat.data(), header_vector,
                          PreheaderSize + fs_info.file_allocation_table_offset,
                          fat.size() * sizeof(FATNode)),
            LOG_ERROR(Core, "File size is too small"));

        return true;
    }

    bool GetFileData(std::vector<u8>& out, std::size_t index) const {
        if (index >= file_entry_table.size()) {
            LOG_ERROR(Core, "Index out of bound {}", index);
            return false;
        }
        auto entry = file_entry_table[index];

        u32 block = entry.data_block_index;
        if (block == 0x80000000) { // empty file
            return true;
        }

        u64 file_size = entry.file_size;
        if (file_size >= 64 * 1024 * 1024) {
            LOG_ERROR(Core, "File size too large");
            return false;
        }

        out.resize(file_size);
        std::size_t written = 0;
        while (true) {
            // Entry index is block index + 1
            auto block_data = fat[block + 1];

            u32 last_block = block;
            if (block_data.v.flag) { // This node has multiple entries
                last_block = fat[block + 2].v.index - 1;
            }

            // offset & size of the data chunk represented by the FAT node
            const auto offset = static_cast<std::ptrdiff_t>(fs_info.data_region_block_size) * block;
            const auto size =
                static_cast<std::size_t>(fs_info.data_region_block_size) * (last_block - block + 1);

            const auto to_write = std::min<std::size_t>(file_size, size);
            TRY(CheckedMemcpy(out.data() + written, data_region, offset, to_write),
                LOG_ERROR(Core, "File data out of bound"));
            file_size -= to_write;
            written += to_write;

            if (block_data.v.index == 0 || file_size == 0) // last node
                break;

            block = block_data.v.index - 1;
        }

        return true;
    }

    bool duplicate_data;
    FullHeader<Preheader> header;
    FileSystemInformation fs_info;
    std::vector<DirectoryEntryType> directory_entry_table;
    std::vector<FileEntryType> file_entry_table;
    std::vector<FATNode> fat;
    std::vector<u8> data_region;
};

/// Parameters of the archive, as specified in the Create or Format call.
struct ArchiveFormatInfo {
    u32_le total_size;         ///< The pre-defined size of the archive.
    u32_le number_directories; ///< The pre-defined number of directories in the archive.
    u32_le number_files;       ///< The pre-defined number of files in the archive.
    u8 duplicate_data;         ///< Whether the archive should duplicate the data.
};
static_assert(std::is_standard_layout_v<ArchiveFormatInfo> && std::is_trivial_v<ArchiveFormatInfo>,
              "ArchiveFormatInfo is not POD");

/**
 * Represents an Archive-like pack where there are directory structures.
 * Has an ExtractDirectory function that recursively extracts directories.
 * Expects implementor to have ExtractFile.
 */
template <typename T>
class Archive : protected InnerFAT<T> {
public:
    bool ExtractDirectory(const std::string& path, std::size_t index) const {
        if (index >= this->directory_entry_table.size()) {
            LOG_ERROR(Core, "Index out of bound {}", index);
            return false;
        }
        const auto& entry = this->directory_entry_table[index];

        std::array<char, 17> name_data = {}; // Append a null terminator
        std::memcpy(name_data.data(), entry.name.data(), entry.name.size());

        const std::string name = name_data.data();
        std::string new_path = name.empty() ? path : path + name + "/"; // Name is empty for root

        if (!FileUtil::CreateFullPath(new_path)) {
            LOG_ERROR(Core, "Could not create path {}", new_path);
            return false;
        }

        // Files
        u32 cur = entry.first_file_index;
        while (cur != 0) {
            if (cur >= this->file_entry_table.size()) {
                LOG_ERROR(Core, "Index out of bound {}", cur);
                return false;
            }
            const auto& file_entry = this->file_entry_table[cur];
            std::memcpy(name_data.data(), file_entry.name.data(), file_entry.name.size());

            if (!static_cast<const T*>(this)->ExtractFile(new_path + std::string{name_data.data()},
                                                          cur)) {
                return false;
            }
            cur = this->file_entry_table[cur].next_sibling_index;
        }

        // Subdirectories
        cur = entry.first_subdirectory_index;
        while (cur != 0) {
            if (!ExtractDirectory(new_path, cur))
                return false;
            cur = this->directory_entry_table[cur].next_sibling_index;
        }

        return true;
    }
};

} // namespace Core
