// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace Core {

#include <array>
#include <type_traits>
#include <vector>
#include "common/bit_field.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

class SDMCDecryptor;

/// Parameters of the archive, as specified in the Create or Format call.
struct ArchiveFormatInfo {
    u32_le total_size;         ///< The pre-defined size of the archive.
    u32_le number_directories; ///< The pre-defined number of directories in the archive.
    u32_le number_files;       ///< The pre-defined number of files in the archive.
    u8 duplicate_data;         ///< Whether the archive should duplicate the data.
};
static_assert(std::is_pod<ArchiveFormatInfo>::value, "ArchiveFormatInfo is not POD");

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

/**
 * Virtual interface for the inner FAT filesystem of SD Savegames/Extdata/TitleDB.
 */
class InnerFAT {
public:
    virtual ~InnerFAT();

    /**
     * Returns whether the filesystem is in "good" state, i.e. successfully initialized.
     */
    bool IsGood() const;

    /**
     * Completely extracts everything from this filesystem, including files, directories
     * and metadata used by Citra.
     * @return true on success, false otherwise
     */
    virtual bool Extract(std::string path) const = 0;

protected:
    /**
     * Gets the ArchiveFormatInfo of this archive, used for writing the archive metadata.
     */
    virtual ArchiveFormatInfo GetFormatInfo() const = 0;

    /**
     * Extracts the index-th file in the file entry table to a certain path. (The path does not
     * contain the file name).
     * @return true on success, false otherwise
     */
    virtual bool ExtractFile(const std::string& path, std::size_t index) const = 0;

    /**
     * Recursively extracts the index-th directory in the directory entry table.
     * @return true on success, false otherwise
     */
    bool ExtractDirectory(const std::string& path, std::size_t index) const;

    /**
     * Writes the corresponding archive metadata to a certain path.
     * @return true on success, false otherwise
     */
    bool WriteMetadata(const std::string& path) const;

    bool is_good = false;
    FATHeader header;
    FileSystemInformation fs_info;
    std::vector<DirectoryEntryTableEntry> directory_entry_table;
    std::vector<FileEntryTableEntry> file_entry_table;
    std::vector<u8> data_region;
};

class SDSavegame : public InnerFAT {
public:
    explicit SDSavegame(std::vector<std::vector<u8>> partitions);
    ~SDSavegame() override;

    bool Extract(std::string path) const override;

private:
    bool Init();
    bool ExtractFile(const std::string& path, std::size_t index) const override;
    ArchiveFormatInfo GetFormatInfo() const override;

    std::vector<FATNode> fat;
    bool duplicate_data; // Layout variant

    // Temporary storage for construction data
    std::vector<u8> data;
    std::vector<u8> partitionA;
    std::vector<u8> partitionB;
};

class SDExtdata : public InnerFAT {
public:
    /**
     * Loads an SD extdata folder.
     * @param data_path Path to the ENCRYPTED SD extdata folder, relative to decryptor root
     * @param decryptor Const reference to the SDMCDecryptor.
     */
    explicit SDExtdata(std::string data_path, const SDMCDecryptor& decryptor);
    ~SDExtdata() override;

    bool Extract(std::string path) const override;

private:
    bool Init();
    bool ExtractFile(const std::string& path, std::size_t index) const override;
    ArchiveFormatInfo GetFormatInfo() const override;

    std::string data_path;
    const SDMCDecryptor& decryptor;
};

} // namespace Core
