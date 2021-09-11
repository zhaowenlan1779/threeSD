// Copyright 2018 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/swap.h"
#include "core/db/seed_db.h"

namespace Core {

bool SeedDB::AddFromFile(const std::string& path) {
    if (!FileUtil::Exists(path)) {
        LOG_WARNING(Service_FS, "Seed database does not exist");
        return true;
    }
    FileUtil::IOFile file{path, "rb"};
    if (!file.IsOpen()) {
        LOG_ERROR(Service_FS, "Failed to open seed database");
        return false;
    }
    u32_le count;
    if (file.ReadBytes(&count, sizeof(count)) != sizeof(count)) {
        LOG_ERROR(Service_FS, "Failed to read seed database count fully");
        return false;
    }
    if (!file.Seek(SEEDDB_PADDING_BYTES, SEEK_CUR)) {
        LOG_ERROR(Service_FS, "Failed to skip seed database padding");
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        u64_le title_id;
        if (!file.ReadBytes(&title_id, sizeof(title_id))) {
            LOG_ERROR(Service_FS, "Failed to read seed {} title ID", i);
            return false;
        }
        Seed seed;
        if (!file.ReadBytes(seed.data(), seed.size())) {
            LOG_ERROR(Service_FS, "Failed to read seed {} data", i);
            return false;
        }
        if (!file.Seek(SEEDDB_ENTRY_PADDING_BYTES, SEEK_CUR)) {
            LOG_ERROR(Service_FS, "Failed to skip past seed {} padding", i);
            return false;
        }
        seeds.emplace(title_id, std::move(seed));
    }
    return true;
}

bool SeedDB::Save(const std::string& path) const {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Service_FS, "Failed to create seed database");
        return false;
    }
    FileUtil::IOFile file{path, "wb"};
    if (!file.IsOpen()) {
        LOG_ERROR(Service_FS, "Failed to open seed database");
        return false;
    }
    u32_le count{static_cast<u32>(seeds.size())};
    if (file.WriteBytes(&count, sizeof(count)) != sizeof(count)) {
        LOG_ERROR(Service_FS, "Failed to write seed database count fully");
        return false;
    }
    std::array<u8, SEEDDB_PADDING_BYTES> padding{};
    if (file.WriteBytes(padding.data(), padding.size()) != padding.size()) {
        LOG_ERROR(Service_FS, "Failed to write seed database padding fully");
        return false;
    }
    for (const auto& [title_id, seed] : seeds) {
        const u64_le raw_title_id{title_id}; // for endianess
        if (file.WriteBytes(&raw_title_id, sizeof(raw_title_id)) != sizeof(raw_title_id)) {
            LOG_ERROR(Service_FS, "Failed to write seed {:016x} title ID fully", title_id);
            return false;
        }

        if (file.WriteBytes(seed.data(), seed.size()) != seed.size()) {
            LOG_ERROR(Service_FS, "Failed to write seed {:016x} data fully", title_id);
            return false;
        }
        static constexpr std::array<u8, SEEDDB_ENTRY_PADDING_BYTES> Padding{};
        if (file.WriteBytes(Padding.data(), Padding.size()) != Padding.size()) {
            LOG_ERROR(Service_FS, "Failed to write seed {:016x} padding fully", title_id);
            return false;
        }
    }
    return true;
}

std::size_t SeedDB::GetSize() const {
    return sizeof(u32) + SEEDDB_PADDING_BYTES + seeds.size() * SEEDDB_ENTRY_SIZE;
}

} // namespace Core
