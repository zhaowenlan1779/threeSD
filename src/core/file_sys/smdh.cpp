// Copyright 2016 Citra Emulator Project / 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "core/file_sys/smdh.h"

namespace Core {

// 8x8 Z-Order coordinate from 2D coordinates
static constexpr u32 MortonInterleave(u32 x, u32 y) {
    constexpr u32 xlut[] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    constexpr u32 ylut[] = {0x00, 0x02, 0x08, 0x0a, 0x20, 0x22, 0x28, 0x2a};
    return xlut[x % 8] + ylut[y % 8];
}

/**
 * Calculates the offset of the position of the pixel in Morton order
 */
static inline u32 GetMortonOffset(u32 x, u32 y, u32 bytes_per_pixel) {
    // Images are split into 8x8 tiles. Each tile is composed of four 4x4 subtiles each
    // of which is composed of four 2x2 subtiles each of which is composed of four texels.
    // Each structure is embedded into the next-bigger one in a diagonal pattern, e.g.
    // texels are laid out in a 2x2 subtile like this:
    // 2 3
    // 0 1
    //
    // The full 8x8 tile has the texels arranged like this:
    //
    // 42 43 46 47 58 59 62 63
    // 40 41 44 45 56 57 60 61
    // 34 35 38 39 50 51 54 55
    // 32 33 36 37 48 49 52 53
    // 10 11 14 15 26 27 30 31
    // 08 09 12 13 24 25 28 29
    // 02 03 06 07 18 19 22 23
    // 00 01 04 05 16 17 20 21
    //
    // This pattern is what's called Z-order curve, or Morton order.

    const unsigned int block_height = 8;
    const unsigned int coarse_x = x & ~7;

    u32 i = MortonInterleave(x, y);

    const unsigned int offset = coarse_x * block_height;

    return (i + offset) * bytes_per_pixel;
}

bool IsValidSMDH(const std::vector<u8>& smdh_data) {
    if (smdh_data.size() < sizeof(Core::SMDH))
        return false;

    u32 magic;
    memcpy(&magic, smdh_data.data(), sizeof(u32));

    return MakeMagic('S', 'M', 'D', 'H') == magic;
}

std::vector<u16> SMDH::GetIcon(bool large) const {
    u32 size;
    const u8* icon_data;

    if (large) {
        size = 48;
        icon_data = large_icon.data();
    } else {
        size = 24;
        icon_data = small_icon.data();
    }

    std::vector<u16> icon(size * size);
    for (u32 x = 0; x < size; ++x) {
        for (u32 y = 0; y < size; ++y) {
            u32 coarse_y = y & ~7;
            const u8* pixel = icon_data + GetMortonOffset(x, y, 2) + coarse_y * size * 2;
            icon[x + size * y] = (pixel[1] << 8) + pixel[0];
        }
    }
    return icon;
}

std::array<u16, 0x40> SMDH::GetShortTitle(Core::SMDH::TitleLanguage language) const {
    return titles[static_cast<int>(language)].short_title;
}

std::string SMDH::GetRegionString() const {
    constexpr u32 REGION_COUNT = 7;

    // JPN/USA/EUR/Australia/CHN/KOR/TWN
    // Australia does not have a symbol because it's practically the same as Europe
    static const std::array<std::string, REGION_COUNT> RegionSymbols{
        {"J", "U", "E", "", "C", "K", "T"}};

    std::string region_string;
    for (u32 region = 0; region < REGION_COUNT; ++region) {
        if (region_lockout & (1 << region)) {
            region_string.append(RegionSymbols[region]);
        }
    }

    if (region_string == "JUECKT") {
        return "W";
    }
    return region_string;
}

} // namespace Core
