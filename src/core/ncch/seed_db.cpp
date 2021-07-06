// Copyright 2018 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Modified from Citra's implementation to allow multiple instances

#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/ncch/seed_db.h"

namespace Core {

bool SeedDB::Load(const std::string& path) {
    seeds.clear();
    if (!FileUtil::Exists(path)) {
        LOG_WARNING(Service_FS, "Seed database does not exist");
        return true;
    }
    FileUtil::IOFile file{path, "rb"};
    if (!file.IsOpen()) {
        LOG_ERROR(Service_FS, "Failed to open seed database");
        return false;
    }
    u32 count;
    if (!file.ReadBytes(&count, sizeof(count))) {
        LOG_ERROR(Service_FS, "Failed to read seed database count fully");
        return false;
    }
    if (!file.Seek(SEEDDB_PADDING_BYTES, SEEK_CUR)) {
        LOG_ERROR(Service_FS, "Failed to skip seed database padding");
        return false;
    }
    for (u32 i = 0; i < count; ++i) {
        Seed seed;
        if (!file.ReadBytes(&seed.title_id, sizeof(seed.title_id))) {
            LOG_ERROR(Service_FS, "Failed to read seed {} title ID", i);
            return false;
        }
        if (!file.ReadBytes(seed.data.data(), seed.data.size())) {
            LOG_ERROR(Service_FS, "Failed to read seed {} data", i);
            return false;
        }
        if (!file.ReadBytes(seed.reserved.data(), seed.reserved.size())) {
            LOG_ERROR(Service_FS, "Failed to read seed {} reserved data", i);
            return false;
        }
        seeds.push_back(seed);
    }
    return true;
}

bool SeedDB::Save(const std::string& path) {
    if (!FileUtil::CreateFullPath(path)) {
        LOG_ERROR(Service_FS, "Failed to create seed database");
        return false;
    }
    FileUtil::IOFile file{path, "wb"};
    if (!file.IsOpen()) {
        LOG_ERROR(Service_FS, "Failed to open seed database");
        return false;
    }
    u32 count{static_cast<u32>(seeds.size())};
    if (file.WriteBytes(&count, sizeof(count)) != sizeof(count)) {
        LOG_ERROR(Service_FS, "Failed to write seed database count fully");
        return false;
    }
    std::array<u8, SEEDDB_PADDING_BYTES> padding{};
    if (file.WriteBytes(padding.data(), padding.size()) != padding.size()) {
        LOG_ERROR(Service_FS, "Failed to write seed database padding fully");
        return false;
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (file.WriteBytes(&seeds[i].title_id, sizeof(seeds[i].title_id)) !=
            sizeof(seeds[i].title_id)) {
            LOG_ERROR(Service_FS, "Failed to write seed {} title ID fully", i);
            return false;
        }
        if (file.WriteBytes(seeds[i].data.data(), seeds[i].data.size()) != seeds[i].data.size()) {
            LOG_ERROR(Service_FS, "Failed to write seed {} data fully", i);
            return false;
        }
        if (file.WriteBytes(seeds[i].reserved.data(), seeds[i].reserved.size()) !=
            seeds[i].reserved.size()) {
            LOG_ERROR(Service_FS, "Failed to write seed {} reserved data fully", i);
            return false;
        }
    }
    return true;
}

void SeedDB::Add(const Seed& seed) {
    seeds.push_back(seed);
}

std::size_t SeedDB::Size() const {
    return seeds.size();
}

std::optional<Seed::Data> SeedDB::Get(u64 title_id) const {
    const auto found_seed_iter =
        std::find_if(seeds.begin(), seeds.end(),
                     [title_id](const auto& seed) { return seed.title_id == title_id; });
    if (found_seed_iter != seeds.end()) {
        return found_seed_iter->data;
    }
    return std::nullopt;
}

namespace Seeds {

static SeedDB g_seeddb;
static bool g_seeddb_loaded = false;

void Load(const std::string& path) {
    g_seeddb_loaded = g_seeddb.Load(path);
}

std::optional<Seed::Data> GetSeed(u64 title_id) {
    if (!g_seeddb_loaded) {
        Load(fmt::format("{}/seeddb.bin", FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir)));
    }
    return g_seeddb.Get(title_id);
}

} // namespace Seeds

} // namespace Core
