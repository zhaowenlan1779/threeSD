// Copyright 2018 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <unordered_map>
#include "common/common_types.h"

namespace Core {

constexpr std::size_t SEEDDB_PADDING_BYTES{12};
constexpr std::size_t SEEDDB_ENTRY_PADDING_BYTES{8};
constexpr std::size_t SEEDDB_ENTRY_SIZE{32};

using Seed = std::array<u8, 16>;
class SeedDB {
public:
    bool AddFromFile(const std::string& path);
    bool Save(const std::string& path) const;
    std::size_t GetSize() const;

    std::unordered_map<u64, Seed> seeds;
};

inline SeedDB g_seed_db;

} // namespace Core
