// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <string_view>
#include "common/alignment.h"
#include "common/assert.h"
#include "core/ncch/cia_common.h"
#include "core/ncch/ticket.h"

namespace Core {

bool Ticket::Load(const std::vector<u8> file_data, std::size_t offset) {
    std::size_t total_size = static_cast<std::size_t>(file_data.size() - offset);
    if (total_size < sizeof(u32))
        return false;

    std::memcpy(&signature_type, &file_data[offset], sizeof(u32));

    // Signature lengths are variable, and the body follows the signature
    u32 signature_size = GetSignatureSize(signature_type);
    if (signature_size == 0) {
        return false;
    }

    // The ticket body start position is rounded to the nearest 0x40 after the signature
    std::size_t body_start = Common::AlignUp(signature_size + sizeof(u32), 0x40);
    std::size_t body_end = body_start + sizeof(Body);

    if (total_size < body_end)
        return false;

    // Read signature + ticket body
    signature.resize(signature_size);
    memcpy(signature.data(), &file_data[offset + sizeof(u32)], signature_size);
    memcpy(&body, &file_data[offset + body_start], sizeof(Body));

    return true;
}

std::vector<u8> Ticket::GetData() const {
    u32 signature_size = GetSignatureSize(signature_type);
    ASSERT(signature_size != 0);

    const std::size_t body_start = Common::AlignUp(signature_size + sizeof(u32), 0x40);
    const std::size_t body_end = body_start + sizeof(Body);

    std::vector<u8> out(body_end);
    std::memcpy(out.data(), &signature_type, sizeof(signature_type));
    std::memcpy(out.data() + sizeof(signature_type), signature.data(), signature.size());
    std::memcpy(out.data() + body_start, &body, sizeof(Body));
    return out;
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
    ticket.signature_type = 0x10004; // RSA_2048 SHA256

    ticket.signature.resize(GetSignatureSize(ticket.signature_type));
    std::memset(ticket.signature.data(), 0xFF, ticket.signature.size());

    auto& body = ticket.body;
    std::memcpy(body.issuer.data(), TicketIssuer.data(), TicketIssuer.size());
    std::memset(body.ecc_public_key.data(), 0xFF, body.ecc_public_key.size());
    body.version = 0x01;
    std::memset(body.title_key.data(), 0xFF, body.title_key.size());
    body.title_id = title_id;
    body.common_key_index = 0x00;
    body.audit = 0x01;
    std::memcpy(body.content_index.data(), TicketContentIndex.data(), TicketContentIndex.size());
    // GodMode9 by default sets all remaining 0x80 bytes to 0xFF
    std::memset(body.content_index.data() + TicketContentIndex.size(), 0xFF, 0x80);
    return ticket;
}

} // namespace Core
