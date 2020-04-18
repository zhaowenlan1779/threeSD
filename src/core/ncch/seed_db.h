// Copyright 2018 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Modified from Citra's implementation to allow multiple instances

#include <array>
#include <optional>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"

namespace Core {

constexpr std::size_t SEEDDB_PADDING_BYTES{12};

struct Seed {
    using Data = std::array<u8, 16>;

    u64_le title_id;
    Data data;
    std::array<u8, 8> reserved;
};

class SeedDB {
public:
    bool Load(const std::string& path);
    bool Save(const std::string& path);

    void Add(const Seed& seed);
    std::size_t Size() const;
    std::optional<Seed::Data> Get(u64 title_id) const;

    auto begin() {
        return seeds.begin();
    }

    auto begin() const {
        return seeds.begin();
    }

    auto end() {
        return seeds.end();
    }

    auto end() const {
        return seeds.end();
    }

private:
    std::vector<Seed> seeds;
};

} // namespace Core
