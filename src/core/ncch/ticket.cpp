// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <string_view>
#include "core/ncch/ticket.h"

namespace Core {

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
    std::memset(ticket.signature.data(), 0xFF, ticket.signature.size());
    std::memcpy(ticket.issuer.data(), TicketIssuer.data(), TicketIssuer.size());
    std::memset(ticket.ecc_public_key.data(), 0xFF, ticket.ecc_public_key.size());
    ticket.version = 0x01;
    std::memset(ticket.title_key.data(), 0xFF, ticket.title_key.size());
    ticket.title_id = title_id;
    ticket.common_key_index = 0x00;
    ticket.audit = 0x01;
    std::memset(ticket.content_index.data(), 0xFF, ticket.content_index.size());
    std::memcpy(ticket.content_index.data(), TicketContentIndex.data(), TicketContentIndex.size());
    return ticket;
}

} // namespace Core
