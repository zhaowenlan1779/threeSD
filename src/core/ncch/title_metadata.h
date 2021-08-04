// Copyright 2017 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"
#include "core/ncch/signature.h"

namespace Core {

enum TMDContentTypeFlag : u16 {
    Encrypted = 1 << 0,
    Disc = 1 << 2,
    CFM = 1 << 3,
    Optional = 1 << 14,
    Shared = 1 << 15
};

enum TMDContentIndex { Main = 0, Manual = 1, DLP = 2 };

/**
 * Helper which implements an interface to read and write Title Metadata (TMD) files.
 * If a file path is provided and the file exists, it can be parsed and used, otherwise
 * it must be created. The TMD file can then be interpreted, modified and/or saved.
 */
class TitleMetadata {
public:
    struct ContentChunk {
        u32_be id;
        u16_be index;
        u16_be type;
        u64_be size;
        std::array<u8, 0x20> hash;
    };

    static_assert(sizeof(ContentChunk) == 0x30, "TMD ContentChunk structure size is wrong");

    struct ContentInfo {
        u16_be index;
        u16_be command_count;
        std::array<u8, 0x20> hash;
    };

    static_assert(sizeof(ContentInfo) == 0x24, "TMD ContentInfo structure size is wrong");

#pragma pack(push, 1)

    struct Body {
        std::array<char, 0x40> issuer;
        u8 version;
        u8 ca_crl_version;
        u8 signer_crl_version;
        u8 reserved;
        u64_be system_version;
        u64_be title_id;
        u32_be title_type;
        u16_be group_id;
        u32_be savedata_size;
        u32_be srl_private_savedata_size;
        std::array<u8, 4> reserved_2;
        u8 srl_flag;
        std::array<u8, 0x31> reserved_3;
        u32_be access_rights;
        u16_be title_version;
        u16_be content_count;
        u16_be boot_content;
        std::array<u8, 2> reserved_4;
        std::array<u8, 0x20> contentinfo_hash;
        std::array<ContentInfo, 64> contentinfo;
    };

    static_assert(sizeof(Body) == 0x9C4, "TMD body structure size is wrong");

#pragma pack(pop)

    bool Load(const std::vector<u8> file_data, std::size_t offset = 0);
    bool Save(FileUtil::IOFile& file);
    bool Save(const std::string& file_path);

    void FixHashes();
    bool VerifyHashes() const;
    bool ValidateSignature() const;

    std::size_t GetSize() const;
    u64 GetTitleID() const;
    u32 GetTitleType() const;
    u16 GetTitleVersion() const;
    u64 GetSystemVersion() const;
    std::size_t GetContentCount() const;
    u32 GetBootContentID() const;
    u32 GetManualContentID() const;
    u32 GetDLPContentID() const;
    u32 GetContentIDByIndex(u16 index) const;
    u16 GetContentTypeByIndex(u16 index) const;
    u64 GetContentSizeByIndex(u16 index) const;
    std::array<u8, 16> GetContentCTRByIndex(u16 index) const;

    ContentChunk& GetContentChunkByID(u32 content_id);
    const ContentChunk& GetContentChunkByID(u32 content_id) const;
    bool HasContentID(u32 content_id) const;

    void SetTitleID(u64 title_id);
    void SetTitleType(u32 type);
    void SetTitleVersion(u16 version);
    void SetSystemVersion(u64 version);
    void AddContentChunk(const ContentChunk& chunk);

    void Print() const;

    Signature signature;
    Body tmd_body;
    std::vector<ContentChunk> tmd_chunks;
};

} // namespace Core
