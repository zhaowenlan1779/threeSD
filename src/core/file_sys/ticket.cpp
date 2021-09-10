// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <string_view>
#include <cryptopp/rsa.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/file_sys/cia_common.h"
#include "core/file_sys/ticket.h"

namespace Core {

bool Ticket::Load(const std::vector<u8> file_data, std::size_t offset) {
    if (!signature.Load(file_data, offset)) {
        return false;
    }
    TRY_MEMCPY(&body, file_data, offset + signature.GetSize(), sizeof(Body));

    // Load content index
    const std::size_t content_index_offset = offset + signature.GetSize() + sizeof(Body);

    struct ContentIndexHeader {
        INSERT_PADDING_BYTES(4);
        u32_be content_index_size;
    };
    ContentIndexHeader header;

    TRY_MEMCPY(&header, file_data, content_index_offset, sizeof(header));
    if (static_cast<u32>(header.content_index_size) > 0x10000) { // sanity limit
        LOG_ERROR(Core, "Content index size too big");
        return false;
    }

    content_index.resize(static_cast<u32>(header.content_index_size));
    TRY_MEMCPY(content_index.data(), file_data, content_index_offset, content_index.size());
    return true;
}

bool Ticket::Save(FileUtil::IOFile& file) const {
    // signature
    if (!signature.Save(file)) {
        return false;
    }

    // body
    if (file.WriteBytes(&body, sizeof(body)) != sizeof(body)) {
        LOG_ERROR(Core, "Failed to write body");
        return false;
    }

    // content index
    if (file.WriteBytes(content_index.data(), content_index.size()) != content_index.size()) {
        LOG_ERROR(Core, "Failed to save content index");
        return false;
    }

    return true;
}

bool Ticket::ValidateSignature() const {
    const auto issuer =
        Common::StringFromFixedZeroTerminatedBuffer(body.issuer.data(), body.issuer.size());
    return signature.Verify(issuer, [this](CryptoPP::PK_MessageAccumulator* message) {
        message->Update(reinterpret_cast<const u8*>(&body), sizeof(body));
        message->Update(content_index.data(), content_index.size());
    });
}

std::size_t Ticket::GetSize() const {
    return signature.GetSize() + sizeof(body) + content_index.size();
}

constexpr std::string_view TicketIssuer = "Root-CA00000003-XS0000000c";

// TODO: Make use of this?
constexpr std::string_view TicketIssuerDev = "Root-CA00000004-XS00000009";

// From GodMode9
constexpr std::array<u8, 44> TicketContentIndex{
    {0x00, 0x01, 0x00, 0x14, 0x00, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x14, 0x00, 0x01, 0x00,
     0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
     0x00, 0x84, 0x00, 0x00, 0x00, 0x84, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

// Values taken from GodMode9
Ticket BuildFakeTicket(u64 title_id) {
    Ticket ticket{};

    ticket.signature.type = 0x10004;     // RSA_2048 SHA256
    ticket.signature.data.resize(0x100); // Size of RSA_2048 signature
    std::memset(ticket.signature.data.data(), 0xFF, ticket.signature.data.size());

    auto& body = ticket.body;
    std::memcpy(body.issuer.data(), TicketIssuer.data(), TicketIssuer.size());
    std::memset(body.ecc_public_key.data(), 0xFF, body.ecc_public_key.size());
    body.version = 0x01;
    std::memset(body.title_key.data(), 0xFF, body.title_key.size());
    body.title_id = title_id;
    body.common_key_index = 0x00;
    body.audit = 0x01;
    std::memcpy(ticket.content_index.data(), TicketContentIndex.data(), TicketContentIndex.size());
    // GodMode9 by default sets all remaining 0x80 bytes to 0xFF
    std::memset(ticket.content_index.data() + TicketContentIndex.size(), 0xFF, 0x80);
    return ticket;
}

} // namespace Core
