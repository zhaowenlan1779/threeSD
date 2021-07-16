// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/swap.h"

namespace Core {

enum PublicKeyType : u32 {
    RSA_4096 = 0,
    RSA_2048 = 1,
    ECC = 2,
};

class Certificate {
public:
    struct Body {
        std::array<char, 0x40> issuer;
        u32_be key_type;
        std::array<char, 0x40> name;
        u32_be expiration_time;
    };
    static_assert(sizeof(Body) == 0x88);

    // Returns: 0 on failure, size of the cert on success
    std::size_t Load(std::vector<u8> file_data, std::size_t offset = 0);

    u32_be signature_type;
    std::vector<u8> signature;
    Body body;
    std::vector<u8> public_key;
};

struct CertsDBHeader {
    u32_le magic;
    INSERT_PADDING_BYTES(4);
    u32_le size;
    INSERT_PADDING_BYTES(4);
};
static_assert(sizeof(CertsDBHeader) == 0x10);

using CertsMap = std::unordered_map<std::string, Certificate>;
bool LoadCertsDB(CertsMap& out, const std::string& path);

} // namespace Core
