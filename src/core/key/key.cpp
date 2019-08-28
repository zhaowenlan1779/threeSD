// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <exception>
#include <optional>
#include <string>
#include <fmt/format.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/key/arithmetic128.h"
#include "core/key/key.h"

namespace Key {

namespace {

// The generator constant was calculated using the 0x39 KeyX and KeyY retrieved from a 3DS and the
// normal key dumped from a Wii U solving the equation:
// NormalKey = (((KeyX ROL 2) XOR KeyY) + constant) ROL 87
// On a real 3DS the generation for the normal key is hardware based, and thus the constant can't
// get dumped . generated normal keys are also not accesible on a 3DS. The used formula for
// calculating the constant is a software implementation of what the hardware generator does.
constexpr AESKey generator_constant = {{0x1F, 0xF9, 0xE9, 0xAA, 0xC5, 0xFE, 0x04, 0x08, 0x02, 0x45,
                                        0x91, 0xDC, 0x5D, 0x52, 0x76, 0x8A}};

struct KeyDesc {
    char key_type;
    std::size_t slot_id;
    // This key is identical to the key with the same key_type and slot_id -1
    bool same_as_before;
};

// TODO: Use this to support manual input of keys
[[maybe_unused]] AESKey HexToKey(const std::string& hex) {
    if (hex.size() < 32) {
        throw std::invalid_argument("hex string is too short");
    }

    AESKey key;
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), 0, 16));
    }

    return key;
}

struct KeySlot {
    std::optional<AESKey> x;
    std::optional<AESKey> y;
    std::optional<AESKey> normal;

    void SetKeyX(std::optional<AESKey> key) {
        x = key;
        GenerateNormalKey();
    }

    void SetKeyY(std::optional<AESKey> key) {
        y = key;
        GenerateNormalKey();
    }

    void SetNormalKey(std::optional<AESKey> key) {
        normal = key;
    }

    void GenerateNormalKey() {
        if (x && y) {
            normal = Lrot128(Add128(Xor128(Lrot128(*x, 2), *y), generator_constant), 87);
        } else {
            normal = {};
        }
    }

    void Clear() {
        x.reset();
        y.reset();
        normal.reset();
    }
};

std::array<KeySlot, KeySlotID::MaxKeySlotID> key_slots;
std::array<std::optional<AESKey>, 6> common_key_y_slots;

std::string KeyToString(AESKey& key) {
    std::string s;
    for (auto pos : key) {
        s += fmt::format("{:02X}", pos);
    }
    return s;
}
} // namespace

void LoadBootromKeys(const std::string& path) {
    constexpr std::array<KeyDesc, 80> keys = {
        {{'X', 0x2C, false}, {'X', 0x2D, true},  {'X', 0x2E, true},  {'X', 0x2F, true},
         {'X', 0x30, false}, {'X', 0x31, true},  {'X', 0x32, true},  {'X', 0x33, true},
         {'X', 0x34, false}, {'X', 0x35, true},  {'X', 0x36, true},  {'X', 0x37, true},
         {'X', 0x38, false}, {'X', 0x39, true},  {'X', 0x3A, true},  {'X', 0x3B, true},
         {'X', 0x3C, false}, {'X', 0x3D, false}, {'X', 0x3E, false}, {'X', 0x3F, false},
         {'Y', 0x4, false},  {'Y', 0x5, false},  {'Y', 0x6, false},  {'Y', 0x7, false},
         {'Y', 0x8, false},  {'Y', 0x9, false},  {'Y', 0xA, false},  {'Y', 0xB, false},
         {'N', 0xC, false},  {'N', 0xD, true},   {'N', 0xE, true},   {'N', 0xF, true},
         {'N', 0x10, false}, {'N', 0x11, true},  {'N', 0x12, true},  {'N', 0x13, true},
         {'N', 0x14, false}, {'N', 0x15, false}, {'N', 0x16, false}, {'N', 0x17, false},
         {'N', 0x18, false}, {'N', 0x19, true},  {'N', 0x1A, true},  {'N', 0x1B, true},
         {'N', 0x1C, false}, {'N', 0x1D, true},  {'N', 0x1E, true},  {'N', 0x1F, true},
         {'N', 0x20, false}, {'N', 0x21, true},  {'N', 0x22, true},  {'N', 0x23, true},
         {'N', 0x24, false}, {'N', 0x25, true},  {'N', 0x26, true},  {'N', 0x27, true},
         {'N', 0x28, true},  {'N', 0x29, false}, {'N', 0x2A, false}, {'N', 0x2B, false},
         {'N', 0x2C, false}, {'N', 0x2D, true},  {'N', 0x2E, true},  {'N', 0x2F, true},
         {'N', 0x30, false}, {'N', 0x31, true},  {'N', 0x32, true},  {'N', 0x33, true},
         {'N', 0x34, false}, {'N', 0x35, true},  {'N', 0x36, true},  {'N', 0x37, true},
         {'N', 0x38, false}, {'N', 0x39, true},  {'N', 0x3A, true},  {'N', 0x3B, true},
         {'N', 0x3C, true},  {'N', 0x3D, false}, {'N', 0x3E, false}, {'N', 0x3F, false}}};

    // Bootrom sets all these keys when executed, but later some of the normal keys get overwritten
    // by other applications e.g. process9. These normal keys thus aren't used by any application
    // and have no value for emulation

    auto file = FileUtil::IOFile(path, "rb");
    if (!file) {
        return;
    }

    const std::size_t length = file.GetSize();
    if (length != 65536) {
        LOG_ERROR(Key, "Bootrom9 size is wrong: {}", length);
        return;
    }

    constexpr std::size_t KEY_SECTION_START = 55760;
    file.Seek(KEY_SECTION_START, SEEK_SET); // Jump to the key section

    AESKey new_key;
    for (const auto& key : keys) {
        if (!key.same_as_before) {
            file.ReadArray(new_key.data(), new_key.size());
            if (!file) {
                LOG_ERROR(Key, "Reading from Bootrom9 failed");
                return;
            }
        }

        LOG_DEBUG(Key, "Loaded Slot{:#02x} Key{}: {}", key.slot_id, key.key_type,
                  KeyToString(new_key));

        switch (key.key_type) {
        case 'X':
            key_slots.at(key.slot_id).SetKeyX(new_key);
            break;
        case 'Y':
            key_slots.at(key.slot_id).SetKeyY(new_key);
            break;
        case 'N':
            key_slots.at(key.slot_id).SetNormalKey(new_key);
            break;
        default:
            LOG_ERROR(Key, "Invalid key type {}", key.key_type);
            break;
        }
    }
}

void LoadMovableSedKeys(const std::string& path) {
    auto file = FileUtil::IOFile(path, "rb");
    if (!file) {
        return;
    }

    const std::size_t length = file.GetSize();
    if (length < 0x120) {
        LOG_ERROR(Key, "movable.sed size is too small: {}", length);
        return;
    }

    constexpr std::size_t KEY_SECTION_START = 0x118;
    file.Seek(KEY_SECTION_START, SEEK_SET); // Jump to the key section

    AESKey key;
    file.ReadArray(key.data(), key.size());
    if (!file) {
        LOG_ERROR(Key, "Reading from movable.sed failed");
        return;
    }

    SetKeyY(0x26, key);
}

void ClearKeys() {
    key_slots = {};
    common_key_y_slots = {};
}

void SetKeyX(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyX(key);
}

void SetKeyY(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyY(key);
}

void SetNormalKey(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetNormalKey(key);
}

bool IsNormalKeyAvailable(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.has_value();
}

AESKey GetNormalKey(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.value_or(AESKey{});
}

void SelectCommonKeyIndex(u8 index) {
    key_slots[KeySlotID::TicketCommonKey].SetKeyY(common_key_y_slots.at(index));
}

} // namespace Key
