// Copyright 2017 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/ncch/title_metadata.h"

namespace Core {

ResultStatus TitleMetadata::Load(const std::vector<u8> file_data, std::size_t offset) {
    std::size_t total_size = static_cast<std::size_t>(file_data.size() - offset);
    if (total_size < sizeof(u32_be))
        return ResultStatus::Error;

    memcpy(&signature_type, &file_data[offset], sizeof(u32_be));

    // Signature lengths are variable, and the body follows the signature
    u32 signature_size = GetSignatureSize(signature_type);
    if (signature_size == 0) {
        return ResultStatus::Error;
    }

    // The TMD body start position is rounded to the nearest 0x40 after the signature
    std::size_t body_start = Common::AlignUp(signature_size + sizeof(u32), 0x40);
    std::size_t body_end = body_start + sizeof(Body);

    if (total_size < body_end)
        return ResultStatus::Error;

    // Read signature + TMD body, then load the amount of ContentChunks specified
    tmd_signature.resize(signature_size);
    memcpy(tmd_signature.data(), &file_data[offset + sizeof(u32_be)], signature_size);
    memcpy(&tmd_body, &file_data[offset + body_start], sizeof(TitleMetadata::Body));

    std::size_t expected_size =
        body_start + sizeof(Body) + static_cast<u16>(tmd_body.content_count) * sizeof(ContentChunk);
    if (total_size < expected_size) {
        LOG_ERROR(Service_FS, "Malformed TMD, expected size 0x{:x}, got 0x{:x}!", expected_size,
                  total_size);
        return ResultStatus::ErrorInvalidFormat;
    }

    for (u16 i = 0; i < tmd_body.content_count; i++) {
        ContentChunk chunk;

        memcpy(&chunk, &file_data[offset + body_end + (i * sizeof(ContentChunk))],
               sizeof(ContentChunk));
        tmd_chunks.push_back(chunk);
    }

    return ResultStatus::Success;
}

u64 TitleMetadata::GetTitleID() const {
    return tmd_body.title_id;
}

u32 TitleMetadata::GetTitleType() const {
    return tmd_body.title_type;
}

u16 TitleMetadata::GetTitleVersion() const {
    return tmd_body.title_version;
}

u64 TitleMetadata::GetSystemVersion() const {
    return tmd_body.system_version;
}

size_t TitleMetadata::GetContentCount() const {
    return tmd_chunks.size();
}

u32 TitleMetadata::GetBootContentID() const {
    return tmd_chunks[TMDContentIndex::Main].id;
}

u32 TitleMetadata::GetManualContentID() const {
    return tmd_chunks[TMDContentIndex::Manual].id;
}

u32 TitleMetadata::GetDLPContentID() const {
    return tmd_chunks[TMDContentIndex::DLP].id;
}

u32 TitleMetadata::GetContentIDByIndex(u16 index) const {
    return tmd_chunks[index].id;
}

u16 TitleMetadata::GetContentTypeByIndex(u16 index) const {
    return tmd_chunks[index].type;
}

u64 TitleMetadata::GetContentSizeByIndex(u16 index) const {
    return tmd_chunks[index].size;
}

std::array<u8, 16> TitleMetadata::GetContentCTRByIndex(u16 index) const {
    std::array<u8, 16> ctr{};
    std::memcpy(ctr.data(), &tmd_chunks[index].index, sizeof(u16));
    return ctr;
}

void TitleMetadata::Print() const {
    LOG_DEBUG(Service_FS, "{} chunks", static_cast<u32>(tmd_body.content_count));

    // Content info describes ranges of content chunks
    LOG_DEBUG(Service_FS, "Content info:");
    for (std::size_t i = 0; i < tmd_body.contentinfo.size(); i++) {
        if (tmd_body.contentinfo[i].command_count == 0)
            break;

        LOG_DEBUG(Service_FS, "    Index {:04X}, Command Count {:04X}",
                  static_cast<u32>(tmd_body.contentinfo[i].index),
                  static_cast<u32>(tmd_body.contentinfo[i].command_count));
    }

    // For each content info, print their content chunk range
    for (std::size_t i = 0; i < tmd_body.contentinfo.size(); i++) {
        u16 index = static_cast<u16>(tmd_body.contentinfo[i].index);
        u16 count = static_cast<u16>(tmd_body.contentinfo[i].command_count);

        if (count == 0)
            continue;

        LOG_DEBUG(Service_FS, "Content chunks for content info index {}:", i);
        for (u16 j = index; j < index + count; j++) {
            // Don't attempt to print content we don't have
            if (j > tmd_body.content_count)
                break;

            const ContentChunk& chunk = tmd_chunks[j];
            LOG_DEBUG(Service_FS, "    ID {:08X}, Index {:04X}, Type {:04x}, Size {:016X}",
                      static_cast<u32>(chunk.id), static_cast<u32>(chunk.index),
                      static_cast<u32>(chunk.type), static_cast<u64>(chunk.size));
        }
    }
}
} // namespace Core
