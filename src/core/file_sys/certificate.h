// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <string>
#include <vector>
#include "common/common_funcs.h"
#include "common/swap.h"
#include "core/file_sys/signature.h"

namespace CryptoPP {
class Integer;
}

namespace FileUtil {
class IOFile;
}

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

    bool Load(std::vector<u8> file_data, std::size_t offset = 0);
    bool Save(FileUtil::IOFile& file) const;
    std::size_t GetSize() const;

    /// (modulus, exponent)
    std::pair<CryptoPP::Integer, CryptoPP::Integer> GetRSAPublicKey() const;

    Signature signature;
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

namespace Certs {

bool Load(const std::string& path);
bool IsLoaded();
const Certificate& Get(const std::string& name);
bool Exists(const std::string& name);

} // namespace Certs

} // namespace Core
