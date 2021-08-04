// Copyright 2018 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/signature.h"

namespace FileUtil {
class IOFile;
}

namespace Core {

class Ticket {
public:
#pragma pack(push, 1)
    struct Body {
        std::array<char, 0x40> issuer;
        std::array<u8, 0x3C> ecc_public_key;
        u8 version;
        u8 ca_crl_version;
        u8 signer_crl_version;
        std::array<u8, 0x10> title_key;
        INSERT_PADDING_BYTES(1);
        u64_be ticket_id;
        u32_be console_id;
        u64_be title_id;
        INSERT_PADDING_BYTES(2);
        u16_be ticket_title_version;
        INSERT_PADDING_BYTES(8);
        u8 license_type;
        u8 common_key_index;
        INSERT_PADDING_BYTES(0x2A);
        u32_be eshop_account_id;
        INSERT_PADDING_BYTES(1);
        u8 audit;
        INSERT_PADDING_BYTES(0x42);
        std::array<u8, 0x40> limits;
        std::array<u8, 0xAC> content_index;
    };
    static_assert(sizeof(Body) == 0x210, "Ticket body structure size is wrong");
#pragma pack(pop)

    bool Load(const std::vector<u8> file_data, std::size_t offset = 0);
    bool Save(FileUtil::IOFile& file) const;
    bool ValidateSignature() const;
    std::size_t GetSize() const;

    Signature signature;
    Body body;
};

Ticket BuildFakeTicket(u64 title_id);

} // namespace Core
