// Copyright 2017 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/swap.h"
#include "core/decryptor.h"
#include "core/importer.h"
#include "core/result_status.h"

namespace Core {

////////////////////////////////////////////////////////////////////////////////////////////////////
/// NCCH (Nintendo Content Container Header) header

struct NCCH_Header {
    u8 signature[0x100];
    u32_le magic;
    u32_le content_size;
    u8 partition_id[8];
    u16_le maker_code;
    u16_le version;
    u8 reserved_0[4];
    u64_le program_id;
    u8 reserved_1[0x10];
    u8 logo_region_hash[0x20];
    u8 product_code[0x10];
    u8 extended_header_hash[0x20];
    u32_le extended_header_size;
    u8 reserved_2[4];
    u8 reserved_flag[3];
    u8 secondary_key_slot;
    u8 platform;
    enum class ContentType : u8 {
        Application = 0,
        SystemUpdate = 1,
        Manual = 2,
        Child = 3,
        Trial = 4,
    };
    union {
        BitField<0, 1, u8> is_data;
        BitField<1, 1, u8> is_executable;
        BitField<2, 3, ContentType> content_type;
    };
    u8 content_unit_size;
    union {
        BitField<0, 1, u8> fixed_key;
        BitField<1, 1, u8> no_romfs;
        BitField<2, 1, u8> no_crypto;
        BitField<5, 1, u8> seed_crypto;
    };
    u32_le plain_region_offset;
    u32_le plain_region_size;
    u32_le logo_region_offset;
    u32_le logo_region_size;
    u32_le exefs_offset;
    u32_le exefs_size;
    u32_le exefs_hash_region_size;
    u8 reserved_3[4];
    u32_le romfs_offset;
    u32_le romfs_size;
    u32_le romfs_hash_region_size;
    u8 reserved_4[4];
    u8 exefs_super_block_hash[0x20];
    u8 romfs_super_block_hash[0x20];
};

static_assert(sizeof(NCCH_Header) == 0x200, "NCCH header structure size is wrong");

////////////////////////////////////////////////////////////////////////////////////////////////////
// ExeFS (executable file system) headers

struct ExeFs_SectionHeader {
    char name[8];
    u32 offset;
    u32 size;
};

struct ExeFs_Header {
    ExeFs_SectionHeader section[8];
    u8 reserved[0x80];
    u8 hashes[8][0x20];
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// ExHeader (executable file system header) headers

struct ExHeader_SystemInfoFlags {
    u8 reserved[5];
    u8 flag;
    u8 remaster_version[2];
};

struct ExHeader_CodeSegmentInfo {
    u32 address;
    u32 num_max_pages;
    u32 code_size;
};

struct ExHeader_CodeSetInfo {
    u8 name[8];
    ExHeader_SystemInfoFlags flags;
    ExHeader_CodeSegmentInfo text;
    u32 stack_size;
    ExHeader_CodeSegmentInfo ro;
    u8 reserved[4];
    ExHeader_CodeSegmentInfo data;
    u32 bss_size;
};

struct ExHeader_DependencyList {
    u8 program_id[0x30][8];
};

struct ExHeader_SystemInfo {
    u64 save_data_size;
    u64_le jump_id;
    u8 reserved_2[0x30];
};

struct ExHeader_StorageInfo {
    union {
        u64_le ext_save_data_id;
        // When using extended savedata access
        // Prefer the ID specified in the most significant bits
        BitField<40, 20, u64> extdata_id3;
        BitField<20, 20, u64> extdata_id4;
        BitField<0, 20, u64> extdata_id5;
    };
    u8 system_save_data_id[8];
    union {
        u64_le storage_accessible_unique_ids;
        // When using extended savedata access
        // Prefer the ID specified in the most significant bits
        BitField<40, 20, u64> extdata_id0;
        BitField<20, 20, u64> extdata_id1;
        BitField<0, 20, u64> extdata_id2;
    };
    u8 access_info[7];
    u8 other_attributes;
};

struct ExHeader_ARM11_SystemLocalCaps {
    u64_le program_id;
    u32_le core_version;
    u8 reserved_flags[2];
    union {
        u8 flags0;
        BitField<0, 2, u8> ideal_processor;
        BitField<2, 2, u8> affinity_mask;
        BitField<4, 4, u8> system_mode;
    };
    u8 priority;
    u8 resource_limit_descriptor[0x10][2];
    ExHeader_StorageInfo storage_info;
    u8 service_access_control[0x20][8];
    u8 ex_service_access_control[0x2][8];
    u8 reserved[0xf];
    u8 resource_limit_category;
};

struct ExHeader_ARM11_KernelCaps {
    u32_le descriptors[28];
    u8 reserved[0x10];
};

struct ExHeader_ARM9_AccessControl {
    u8 descriptors[15];
    u8 descversion;
};

struct ExHeader_Header {
    ExHeader_CodeSetInfo codeset_info;
    ExHeader_DependencyList dependency_list;
    ExHeader_SystemInfo system_info;
    ExHeader_ARM11_SystemLocalCaps arm11_system_local_caps;
    ExHeader_ARM11_KernelCaps arm11_kernel_caps;
    ExHeader_ARM9_AccessControl arm9_access_control;
    struct {
        u8 signature[0x100];
        u8 ncch_public_key_modulus[0x100];
        ExHeader_ARM11_SystemLocalCaps arm11_system_local_caps;
        ExHeader_ARM11_KernelCaps arm11_kernel_caps;
        ExHeader_ARM9_AccessControl arm9_access_control;
    } access_desc;
};

static_assert(sizeof(ExHeader_Header) == 0x800, "ExHeader structure size is wrong");

/**
 * Helper which implements an interface to deal with NCCH containers which can
 * contain ExeFS archives or RomFS archives for games or other applications.
 *
 * Note that this is heavily stripped down and can only read (primary-key
 * encrypted non-code sections of) ExeFS and ExHeader by design.
 */
class NCCHContainer {
public:
    /**
     * Constructs the container.
     * @param root_folder Path to SDMC folder
     * @param filepath Path relative to SDMC folder, starting with /
     */
    NCCHContainer(const std::string& root_folder, const std::string& filepath);
    NCCHContainer() {}

    ResultStatus OpenFile(const std::string& root_folder, const std::string& filepath);

    /**
     * Ensure ExeFS and exheader is loaded and ready for reading sections
     * @return ResultStatus result of function
     */
    ResultStatus Load();

    /**
     * Reads an application ExeFS section of an NCCH file (non-compressed, primary key only)
     * @param name Name of section to read out of NCCH file
     * @param buffer Vector to read data into
     * @return ResultStatus result of function
     */
    ResultStatus LoadSectionExeFS(const char* name, std::vector<u8>& buffer);

    /**
     * Get the Program ID of the NCCH container
     * @return ResultStatus result of function
     */
    ResultStatus ReadProgramId(u64_le& program_id);

    /**
     * Get the Extdata ID of the NCCH container
     * @return ResultStatus result of function
     */
    ResultStatus ReadExtdataId(u64& extdata_id);

    /**
     * Checks whether the NCCH container contains an ExeFS
     * @return bool check result
     */
    bool HasExeFS();

    /**
     * Checks whether the NCCH container contains an ExHeader
     * @return bool check result
     */
    bool HasExHeader();

    /**
     * Gets encryption type (which key is used).
     * @return ResultStatus result of function.
     */
    ResultStatus ReadEncryptionType(EncryptionType& encryption);

    /**
     * Gets whether seed crypto is used.
     * @return ResultStatus result of function.
     */
    ResultStatus ReadSeedCrypto(bool& used);

    NCCH_Header ncch_header;
    ExHeader_Header exheader_header;
    ExeFs_Header exefs_header;

private:
    bool has_header = false;
    bool has_exheader = false;
    bool has_exefs = false;
    bool has_romfs = false;

    bool is_loaded = false;

    bool is_encrypted = false;
    // for decrypting exheader, exefs header and icon/banner section
    std::array<u8, 16> primary_key{};
    std::array<u8, 16> secondary_key{}; // for decrypting romfs and .code section
    std::array<u8, 16> exheader_ctr{};
    std::array<u8, 16> exefs_ctr{};
    std::array<u8, 16> romfs_ctr{};

    u32 exefs_offset = 0;

    std::string root_folder;
    std::string filepath;
    SDMCFile file;
    SDMCFile exefs_file;
};

/**
 * Extracts the shared RomFS from a NCCH image.
 * Used for handling system archives.
 */
std::vector<u8> LoadSharedRomFS(const std::vector<u8>& data);

} // namespace Core
