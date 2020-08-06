// Copyright 2017 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <memory>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/data_container.h"
#include "core/importer.h"
#include "core/key/key.h"
#include "core/ncch/ncch_container.h"
#include "core/ncch/seed_db.h"

namespace Core {

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

static const int kMaxSections = 8;   ///< Maximum number of sections (files) in an ExeFs
static const int kBlockSize = 0x200; ///< Size of ExeFS blocks (in bytes)

NCCHContainer::NCCHContainer(std::shared_ptr<FileUtil::IOFile> file_) : file(std::move(file_)) {}

ResultStatus NCCHContainer::OpenFile(std::shared_ptr<FileUtil::IOFile> file_) {
    file = std::move(file_);

    if (!file->IsOpen()) {
        LOG_WARNING(Service_FS, "Failed to open");
        return ResultStatus::Error;
    }

    LOG_DEBUG(Service_FS, "Opened");
    return ResultStatus::Success;
}

ResultStatus NCCHContainer::Load() {
    if (is_loaded)
        return ResultStatus::Success;

    if (file->IsOpen()) {
        // Reset read pointer in case this file has been read before.
        file->Seek(0, SEEK_SET);

        if (file->ReadBytes(&ncch_header, sizeof(NCCH_Header)) != sizeof(NCCH_Header))
            return ResultStatus::Error;

        // Verify we are loading the correct file type...
        if (MakeMagic('N', 'C', 'C', 'H') != ncch_header.magic)
            return ResultStatus::ErrorInvalidFormat;

        has_header = true;
        bool failed_to_decrypt = false;
        if (!ncch_header.no_crypto) {
            is_encrypted = true;

            // Find primary and secondary keys
            if (ncch_header.fixed_key) {
                LOG_DEBUG(Service_FS, "Fixed-key crypto");
                primary_key.fill(0);
                secondary_key.fill(0);
            } else {
                std::array<u8, 16> key_y_primary, key_y_secondary;

                std::copy(ncch_header.signature, ncch_header.signature + key_y_primary.size(),
                          key_y_primary.begin());

                if (!ncch_header.seed_crypto) {
                    key_y_secondary = key_y_primary;
                } else {
                    auto opt{Seeds::GetSeed(ncch_header.program_id)};
                    if (!opt.has_value()) {
                        LOG_ERROR(Service_FS, "Seed for program {:016X} not found",
                                  ncch_header.program_id);
                        failed_to_decrypt = true;
                    } else {
                        auto seed{*opt};
                        std::array<u8, 32> input;
                        std::memcpy(input.data(), key_y_primary.data(), key_y_primary.size());
                        std::memcpy(input.data() + key_y_primary.size(), seed.data(), seed.size());
                        CryptoPP::SHA256 sha;
                        std::array<u8, CryptoPP::SHA256::DIGESTSIZE> hash;
                        sha.CalculateDigest(hash.data(), input.data(), input.size());
                        std::memcpy(key_y_secondary.data(), hash.data(), key_y_secondary.size());
                    }
                }

                Key::SetKeyY(Key::NCCHSecure1, key_y_primary);
                if (!Key::IsNormalKeyAvailable(Key::NCCHSecure1)) {
                    LOG_ERROR(Service_FS, "Secure1 KeyX missing");
                    failed_to_decrypt = true;
                }
                primary_key = Key::GetNormalKey(Key::NCCHSecure1);

                const auto SetSecondaryKey = [this, &failed_to_decrypt,
                                              &key_y_secondary](Key::KeySlotID slot) {
                    Key::SetKeyY(slot, key_y_secondary);
                    if (!Key::IsNormalKeyAvailable(slot)) {
                        LOG_ERROR(Service_FS, "{:#04X} KeyX missing", slot);
                        failed_to_decrypt = true;
                    }
                    secondary_key = Key::GetNormalKey(slot);
                };

                switch (ncch_header.secondary_key_slot) {
                case 0:
                    LOG_DEBUG(Service_FS, "Secure1 crypto");
                    SetSecondaryKey(Key::NCCHSecure1);
                    break;
                case 1:
                    LOG_DEBUG(Service_FS, "Secure2 crypto");
                    SetSecondaryKey(Key::NCCHSecure2);
                    break;
                case 10:
                    LOG_DEBUG(Service_FS, "Secure3 crypto");
                    SetSecondaryKey(Key::NCCHSecure3);
                    break;
                case 11:
                    LOG_DEBUG(Service_FS, "Secure4 crypto");
                    SetSecondaryKey(Key::NCCHSecure4);
                    break;
                }
            }

            // Find CTR for each section
            // Written with reference to
            // https://github.com/d0k3/GodMode9/blob/99af6a73be48fa7872649aaa7456136da0df7938/arm9/source/game/ncch.c#L34-L52
            if (ncch_header.version == 0 || ncch_header.version == 2) {
                LOG_DEBUG(Loader, "NCCH version 0/2");
                // In this version, CTR for each section is a magic number prefixed by partition ID
                // (reverse order)
                std::reverse_copy(ncch_header.partition_id, ncch_header.partition_id + 8,
                                  exheader_ctr.begin());
                exefs_ctr = romfs_ctr = exheader_ctr;
                exheader_ctr[8] = 1;
                exefs_ctr[8] = 2;
                romfs_ctr[8] = 3;
            } else if (ncch_header.version == 1) {
                LOG_DEBUG(Loader, "NCCH version 1");
                // In this version, CTR for each section is the section offset prefixed by partition
                // ID, as if the entire NCCH image is encrypted using a single CTR stream.
                std::copy(ncch_header.partition_id, ncch_header.partition_id + 8,
                          exheader_ctr.begin());
                exefs_ctr = romfs_ctr = exheader_ctr;
                auto u32ToBEArray = [](u32 value) -> std::array<u8, 4> {
                    return std::array<u8, 4>{
                        static_cast<u8>(value >> 24),
                        static_cast<u8>((value >> 16) & 0xFF),
                        static_cast<u8>((value >> 8) & 0xFF),
                        static_cast<u8>(value & 0xFF),
                    };
                };
                auto offset_exheader = u32ToBEArray(0x200); // exheader offset
                auto offset_exefs = u32ToBEArray(ncch_header.exefs_offset * kBlockSize);
                auto offset_romfs = u32ToBEArray(ncch_header.romfs_offset * kBlockSize);
                std::copy(offset_exheader.begin(), offset_exheader.end(),
                          exheader_ctr.begin() + 12);
                std::copy(offset_exefs.begin(), offset_exefs.end(), exefs_ctr.begin() + 12);
                std::copy(offset_romfs.begin(), offset_romfs.end(), romfs_ctr.begin() + 12);
            } else {
                LOG_ERROR(Service_FS, "Unknown NCCH version {}", ncch_header.version);
                failed_to_decrypt = true;
            }
        } else {
            LOG_DEBUG(Service_FS, "No crypto");
            is_encrypted = false;
        }

        // System archives and DLC don't have an extended header but have RomFS
        if (ncch_header.extended_header_size) {
            auto read_exheader = [this](FileUtil::IOFile& file) {
                const std::size_t size = sizeof(exheader_header);
                return file && file.ReadBytes(&exheader_header, size) == size;
            };

            if (!read_exheader(*file)) {
                return ResultStatus::Error;
            }

            if (is_encrypted) {
                // This ID check is masked to low 32-bit as a toleration to ill-formed ROM created
                // by merging games and its updates.
                if ((exheader_header.system_info.jump_id & 0xFFFFFFFF) ==
                    (ncch_header.program_id & 0xFFFFFFFF)) {
                    LOG_WARNING(Service_FS, "NCCH is marked as encrypted but with decrypted "
                                            "exheader. Force no crypto scheme.");
                    is_encrypted = false;
                } else {
                    if (failed_to_decrypt) {
                        LOG_ERROR(Service_FS, "Failed to decrypt");
                        return ResultStatus::ErrorEncrypted;
                    }
                    CryptoPP::byte* data = reinterpret_cast<CryptoPP::byte*>(&exheader_header);
                    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption(
                        primary_key.data(), primary_key.size(), exheader_ctr.data())
                        .ProcessData(data, data, sizeof(exheader_header));
                }
            }

            u32 entry_point = exheader_header.codeset_info.text.address;
            u32 code_size = exheader_header.codeset_info.text.code_size;
            u32 stack_size = exheader_header.codeset_info.stack_size;
            u32 bss_size = exheader_header.codeset_info.bss_size;
            u32 core_version = exheader_header.arm11_system_local_caps.core_version;
            u8 priority = exheader_header.arm11_system_local_caps.priority;
            u8 resource_limit_category =
                exheader_header.arm11_system_local_caps.resource_limit_category;

            LOG_DEBUG(Service_FS, "Name:                        {}",
                      exheader_header.codeset_info.name);
            LOG_DEBUG(Service_FS, "Program ID:                  {:016X}", ncch_header.program_id);
            LOG_DEBUG(Service_FS, "Entry point:                 0x{:08X}", entry_point);
            LOG_DEBUG(Service_FS, "Code size:                   0x{:08X}", code_size);
            LOG_DEBUG(Service_FS, "Stack size:                  0x{:08X}", stack_size);
            LOG_DEBUG(Service_FS, "Bss size:                    0x{:08X}", bss_size);
            LOG_DEBUG(Service_FS, "Core version:                {}", core_version);
            LOG_DEBUG(Service_FS, "Thread priority:             0x{:X}", priority);
            LOG_DEBUG(Service_FS, "Resource limit category:     {}", resource_limit_category);
            LOG_DEBUG(Service_FS, "System Mode:                 {}",
                      static_cast<int>(exheader_header.arm11_system_local_caps.system_mode));

            has_exheader = true;
        }

        // DLC can have an ExeFS and a RomFS but no extended header
        if (ncch_header.exefs_size) {
            exefs_offset = ncch_header.exefs_offset * kBlockSize;
            u32 exefs_size = ncch_header.exefs_size * kBlockSize;

            LOG_DEBUG(Service_FS, "ExeFS offset:                0x{:08X}", exefs_offset);
            LOG_DEBUG(Service_FS, "ExeFS size:                  0x{:08X}", exefs_size);
            file->Seek(exefs_offset, SEEK_SET);
            if (file->ReadBytes(&exefs_header, sizeof(ExeFs_Header)) != sizeof(ExeFs_Header))
                return ResultStatus::Error;

            if (is_encrypted) {
                CryptoPP::byte* data = reinterpret_cast<CryptoPP::byte*>(&exefs_header);
                CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption(primary_key.data(),
                                                              primary_key.size(), exefs_ctr.data())
                    .ProcessData(data, data, sizeof(exefs_header));
            }

            exefs_file = file;
            has_exefs = true;
        }

        if (ncch_header.romfs_offset != 0 && ncch_header.romfs_size != 0)
            has_romfs = true;
    }

    is_loaded = true;
    return ResultStatus::Success;
}

ResultStatus NCCHContainer::LoadSectionExeFS(const char* name, std::vector<u8>& buffer) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!exefs_file || !exefs_file->IsOpen())
        return ResultStatus::Error;

    LOG_DEBUG(Service_FS, "{} sections:", kMaxSections);
    // Iterate through the ExeFs archive until we find a section with the specified name...
    for (unsigned section_number = 0; section_number < kMaxSections; section_number++) {
        const auto& section = exefs_header.section[section_number];

        if (strcmp(section.name, name) == 0) {
            LOG_DEBUG(Service_FS, "{} - offset: 0x{:08X}, size: 0x{:08X}, name: {}", section_number,
                      section.offset, section.size, section.name);

            s64 section_offset = (section.offset + exefs_offset + sizeof(ExeFs_Header));
            exefs_file->Seek(section_offset, SEEK_SET);

            CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption dec(primary_key.data(),
                                                              primary_key.size(), exefs_ctr.data());
            dec.Seek(section.offset + sizeof(ExeFs_Header));

            buffer.resize(section.size);
            if (exefs_file->ReadBytes(&buffer[0], section.size) != section.size)
                return ResultStatus::Error;
            if (is_encrypted) {
                dec.ProcessData(&buffer[0], &buffer[0], section.size);
            }

            return ResultStatus::Success;
        }
    }
    return ResultStatus::ErrorNotUsed;
}

ResultStatus NCCHContainer::ReadProgramId(u64_le& program_id) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_header)
        return ResultStatus::ErrorNotUsed;

    program_id = ncch_header.program_id;
    return ResultStatus::Success;
}

ResultStatus NCCHContainer::ReadExtdataId(u64& extdata_id) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_exheader)
        return ResultStatus::ErrorNotUsed;

    if (exheader_header.arm11_system_local_caps.storage_info.other_attributes >> 1) {
        // Using extended save data access
        // There would be multiple possible extdata IDs in this case. The best we can do for now is
        // guessing that the first one would be the main save.
        const std::array<u64, 6> extdata_ids{{
            exheader_header.arm11_system_local_caps.storage_info.extdata_id0.Value(),
            exheader_header.arm11_system_local_caps.storage_info.extdata_id1.Value(),
            exheader_header.arm11_system_local_caps.storage_info.extdata_id2.Value(),
            exheader_header.arm11_system_local_caps.storage_info.extdata_id3.Value(),
            exheader_header.arm11_system_local_caps.storage_info.extdata_id4.Value(),
            exheader_header.arm11_system_local_caps.storage_info.extdata_id5.Value(),
        }};
        for (u64 id : extdata_ids) {
            if (id) {
                // Found a non-zero ID, use it
                extdata_id = id;
                return ResultStatus::Success;
            }
        }

        return ResultStatus::ErrorNotUsed;
    }

    extdata_id = exheader_header.arm11_system_local_caps.storage_info.ext_save_data_id;
    return ResultStatus::Success;
}

bool NCCHContainer::HasExeFS() {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return false;

    return has_exefs;
}

bool NCCHContainer::HasExHeader() {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return false;

    return has_exheader;
}

ResultStatus NCCHContainer::ReadCodesetName(std::string& name) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_exheader)
        return ResultStatus::ErrorNotUsed;

    std::array<char, 9> name_data{};
    std::memcpy(name_data.data(), exheader_header.codeset_info.name, 8);
    name = name_data.data();
    return ResultStatus::Success;
}

ResultStatus NCCHContainer::ReadEncryptionType(EncryptionType& encryption) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_header)
        return ResultStatus::ErrorNotUsed;

    if (!is_encrypted) {
        encryption = EncryptionType::None;
    } else if (ncch_header.fixed_key) {
        encryption = EncryptionType::FixedKey;
    } else {
        switch (ncch_header.secondary_key_slot) {
        case 0:
            encryption = EncryptionType::NCCHSecure1;
            break;
        case 1:
            encryption = EncryptionType::NCCHSecure2;
            break;
        case 10:
            encryption = EncryptionType::NCCHSecure3;
            break;
        case 11:
            encryption = EncryptionType::NCCHSecure4;
            break;
        default:
            LOG_ERROR(Service_FS, "Unknown encryption type {:X}!", ncch_header.secondary_key_slot);
            return ResultStatus::ErrorNotUsed;
        }
    }

    return ResultStatus::Success;
}

ResultStatus NCCHContainer::ReadSeedCrypto(bool& used) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_header)
        return ResultStatus::ErrorNotUsed;

    used = ncch_header.seed_crypto;
    return ResultStatus::Success;
}

ResultStatus NCCHContainer::DecryptToFile(std::shared_ptr<FileUtil::IOFile> dest_file,
                                          const ProgressCallback& callback) {
    ResultStatus result = Load();
    if (result != ResultStatus::Success)
        return result;

    if (!has_header)
        return ResultStatus::ErrorNotUsed;

    if (!*dest_file) {
        LOG_ERROR(Core, "File is not open");
        return ResultStatus::Error;
    }

    if (!is_encrypted) {
        // Simply copy everything
        file->Seek(0, SEEK_SET);

        const auto size = file->GetSize();
        decryptor.Reset(size);

        const bool ret = decryptor.DecryptAndWriteFile(file, size, dest_file, callback);
        return ret ? ResultStatus::Success : ResultStatus::Error;
    }

    auto total_size = file->GetSize() - sizeof(NCCH_Header);
    // These are written directly instead of using the decryptor
    if (has_exheader) {
        total_size -= sizeof(ExHeader_Header);
    }
    if (has_exefs) {
        total_size -= sizeof(ExeFs_Header);
    }
    decryptor.Reset(total_size);

    std::size_t written{};

    // Write NCCH header
    NCCH_Header modified_header = ncch_header;
    modified_header.raw_crypto_flags = 0x4; // No crypto
    modified_header.secondary_key_slot = 0;
    if (dest_file->WriteBytes(&modified_header, sizeof(modified_header)) !=
        sizeof(modified_header)) {
        LOG_ERROR(Core, "Could not write NCCH header to file");
        return ResultStatus::Error;
    }
    written += sizeof(NCCH_Header);

    // Write Exheader
    if (has_exheader) {
        if (dest_file->WriteBytes(&exheader_header, sizeof(exheader_header)) !=
            sizeof(exheader_header)) {
            LOG_ERROR(Core, "Could not write Exheader to file");
            return ResultStatus::Error;
        }
        written += sizeof(ExHeader_Header);
    }

    const auto Write = [&](std::string_view name, std::size_t offset, std::size_t size,
                           bool decrypt = false, const Key::AESKey& key = {},
                           const Key::AESKey& ctr = {}, std::size_t aes_seek_pos = 0) {
        if (offset == 0 || size == 0) {
            return true;
        }

        if (aborted.exchange(false)) {
            return false;
        }
        ASSERT_MSG(written <= offset, "Offsets are not in increasing order");

        // Zero out the gap
        const std::array<u8, 1024> zeroes{};
        std::size_t zeroes_left = offset - written;
        while (zeroes_left > 0) {
            const auto to_write = std::min(zeroes.size(), zeroes_left);
            if (dest_file->WriteBytes(zeroes.data(), to_write) != to_write) {
                LOG_ERROR(Core, "Could not write zeroes before {}", name);
                return false;
            }
            zeroes_left -= to_write;
        }

        file->Seek(offset, SEEK_SET);

        if (aborted.exchange(false)) {
            return false;
        }
        if (!decryptor.DecryptAndWriteFile(file, size, dest_file, callback, decrypt, key, ctr,
                                           aes_seek_pos)) {
            LOG_ERROR(Core, "Could not write {}", name);
            return false;
        }
        written = offset + size;
        return true;
    };

    if (!Write("logo", ncch_header.logo_region_offset * 0x200,
               ncch_header.logo_region_size * 0x200)) {
        return ResultStatus::Error;
    }

    if (!Write("plain region", ncch_header.plain_region_offset * 0x200,
               ncch_header.plain_region_size * 0x200)) {
        return ResultStatus::Error;
    }

    // Write ExeFS header
    if (has_exefs) {
        if (dest_file->WriteBytes(&exefs_header, sizeof(exefs_header)) != sizeof(exefs_header)) {
            LOG_ERROR(Core, "Could not write ExeFS header to file");
            return ResultStatus::Error;
        }
        written += sizeof(ExeFs_Header);

        for (unsigned section_number = 0; section_number < kMaxSections; section_number++) {
            const auto& section = exefs_header.section[section_number];
            if (section.offset == 0 && section.size == 0) { // not used
                continue;
            }

            Key::AESKey key;
            if (strcmp(section.name, "icon") == 0 || strcmp(section.name, "banner") == 0) {
                key = primary_key;
            } else {
                key = secondary_key;
            }

            // Plus 1 for the ExeFS header
            if (!Write(section.name, section.offset + (ncch_header.exefs_offset + 1) * 0x200,
                       section.size, true, key, exefs_ctr, section.offset + sizeof(exefs_header))) {
                return ResultStatus::Error;
            }
        }
    }

    if (has_romfs && !Write("romfs", ncch_header.romfs_offset * 0x200,
                            ncch_header.romfs_size * 0x200, true, secondary_key, romfs_ctr)) {
        return ResultStatus::Error;
    }
    if (written < file->GetSize()) {
        LOG_WARNING(Core, "Data after {} ignored", written);
    }

    callback(total_size, total_size);
    return ResultStatus::Success;
}

void NCCHContainer::AbortDecryptToFile() {
    aborted = true;
    decryptor.Abort();
}

#pragma pack(push, 1)
struct RomFSIVFCHeader {
    u32_le magic;
    u32_le version;
    u32_le master_hash_size;
    std::array<LevelDescriptor, 3> levels;
    INSERT_PADDING_BYTES(0xC);
};
static_assert(sizeof(RomFSIVFCHeader) == 0x60, "Size of RomFSIVFCHeader is incorrect");
#pragma pack(pop)

std::vector<u8> LoadSharedRomFS(const std::vector<u8>& data) {
    NCCH_Header header;
    ASSERT_MSG(data.size() >= sizeof(header), "NCCH size is too small");
    std::memcpy(&header, data.data(), sizeof(header));

    const std::size_t offset = header.romfs_offset * 0x200; // 0x200: Media unit
    RomFSIVFCHeader ivfc;
    ASSERT_MSG(data.size() >= offset + sizeof(ivfc), "NCCH size is too small");
    std::memcpy(&ivfc, data.data() + offset, sizeof(ivfc));

    ASSERT_MSG(ivfc.magic == MakeMagic('I', 'V', 'F', 'C'), "IVFC magic is incorrect");
    ASSERT_MSG(ivfc.version == 0x10000, "IVFC version is incorrect");

    std::vector<u8> result(ivfc.levels[2].size);

    // Calculation from ctrtool
    const std::size_t data_offset =
        offset + Common::AlignUp(sizeof(ivfc) + ivfc.master_hash_size,
                                 std::pow(2, ivfc.levels[2].block_size));
    ASSERT_MSG(data.size() >= data_offset + ivfc.levels[2].size);
    std::memcpy(result.data(), data.data() + data_offset, ivfc.levels[2].size);

    return result;
}

} // namespace Core
