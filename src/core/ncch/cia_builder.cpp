// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "core/importer.h"
#include "core/ncch/cia_builder.h"
#include "core/ncch/ticket.h"
#include "core/ncch/title_metadata.h"
#include "core/title_db.h"
#include "core/title_keys_bin.h"

namespace Core {

constexpr std::size_t CIA_ALIGNMENT = 0x40;

class HashedFile : public FileUtil::IOFile {
public:
    explicit HashedFile(const std::string& filename, const char openmode[], int flags = 0)
        : FileUtil::IOFile(filename, openmode, flags) {}
    ~HashedFile() override = default;

    void SetHashEnabled(bool enabled) {
        hash_enabled = enabled;
        if (enabled) { // Restart when hash is newly restarted
            sha.Restart();
        }
    }

    void GetHash(u8* out) {
        sha.Final(out);
    }

    std::size_t Write(const char* data, std::size_t length) override {
        const std::size_t length_written = FileUtil::IOFile::Write(data, length);
        sha.Update(reinterpret_cast<const CryptoPP::byte*>(data), length_written);
        return length_written;
    }

private:
    CryptoPP::SHA256 sha;
    bool hash_enabled{};
};

CIABuilder::CIABuilder() = default;
CIABuilder::~CIABuilder() = default;

bool CIABuilder::Init(const std::string& destination, TitleMetadata tmd_, const Config& config,
                      std::size_t total_size_, const Common::ProgressCallback& callback_) {

    header = {};
    meta = {};

    file = std::make_shared<HashedFile>(destination, "wb");
    if (!*file) {
        LOG_ERROR(Core, "Could not open file {}", destination);
        file.reset();
        return false;
    }

    tmd = std::move(tmd_);
    // Remove encrypted flag from TMD chunks
    for (auto& chunk : tmd.tmd_chunks) {
        chunk.type &= ~0x01;
    }

    header.header_size = sizeof(header);
    // Header will be written in Finalize

    // Cert
    cert_offset = Common::AlignUp(header.header_size, CIA_ALIGNMENT);
    header.cert_size = CIA_CERT_SIZE;
    if (!WriteCert(config.certs_db_path)) {
        LOG_ERROR(Core, "Could not write cert to file {}", destination);
        file.reset();
        return false;
    }

    // Ticket
    ticket_offset = Common::AlignUp(cert_offset + header.cert_size, CIA_ALIGNMENT);
    if (!WriteTicket(config.ticket_db_path, config.enc_title_keys_bin_path)) {
        file.reset();
        return false;
    }

    // TMD will be written in Finalize (we need to set content hash, etc)
    tmd_offset = Common::AlignUp(ticket_offset + header.tik_size, CIA_ALIGNMENT);
    header.tmd_size = tmd.GetSize();

    content_offset = Common::AlignUp(tmd_offset + header.tmd_size, CIA_ALIGNMENT);
    header.content_size = 0;

    // Meta will be written in Finalize
    header.meta_size = 0;

    written = content_offset;
    total_size = total_size_;
    callback = callback_;

    callback(written, total_size);
    return true;
}

bool CIABuilder::WriteCert(const std::string& certs_db_path) {
    FileUtil::IOFile certs_db(certs_db_path, "rb");
    if (!certs_db) {
        LOG_ERROR(Core, "Could not open {}", certs_db_path);
        return false;
    }

    std::array<u8, CIA_CERT_SIZE> cert;
    // Read CIA cert
    certs_db.Seek(0x0C10, SEEK_SET);
    if (certs_db.ReadBytes(cert.data(), 0x1F0) != 0x1F0) {
        return false;
    }

    certs_db.Seek(0x3A00, SEEK_SET);
    if (certs_db.ReadBytes(cert.data() + 0x1F0, 0x210) != 0x210) {
        return false;
    }

    certs_db.Seek(0x3F10, SEEK_SET);
    if (certs_db.ReadBytes(cert.data() + 0x400, 0x300) != 0x300) {
        return false;
    }

    certs_db.Seek(0x3C10, SEEK_SET);
    if (certs_db.ReadBytes(cert.data() + 0x700, 0x300) != 0x300) {
        return false;
    }

    // Write CIA cert to file
    file->Seek(cert_offset, SEEK_SET);
    if (file->WriteBytes(cert.data(), cert.size()) != cert.size()) {
        LOG_ERROR(Core, "Could not write cert");
        return false;
    }
    return true;
}

bool CIABuilder::WriteTicket(const std::string& ticket_db_path,
                             const std::string& enc_title_keys_bin_path) {
    const auto title_id = tmd.GetTitleID();
    Ticket ticket = BuildFakeTicket(title_id);

    // Fill in common_key_index and title_key from either ticket.db (installed tickets)
    // or GM9 support files (encTitleKeys.bin) found on the SD card
    if (TicketDB ticket_db(ticket_db_path);
        ticket_db.IsGood() && ticket_db.tickets.count(title_id)) { // ticket.db

        const auto& legit_ticket = ticket_db.tickets.at(title_id);
        ticket.body.common_key_index = legit_ticket.body.common_key_index;
        ticket.body.title_key = legit_ticket.body.title_key;

    } else if (TitleKeysBin enc_title_keys(enc_title_keys_bin_path);
               enc_title_keys.IsGood() && enc_title_keys.entries.count(title_id)) { // support files

        const auto& entry = enc_title_keys.entries.at(title_id);
        ticket.body.common_key_index = entry.common_key_index;
        ticket.body.title_key = entry.title_key;
    } else {
        LOG_WARNING(Core, "Could not find title key for {:016x}", title_id);
    }

    const auto ticket_data = ticket.GetData();
    header.tik_size = ticket_data.size();

    file->Seek(ticket_offset, SEEK_SET);
    if (file->WriteBytes(ticket_data.data(), ticket_data.size()) != ticket_data.size()) {
        LOG_ERROR(Core, "Could not write ticket");
        file.reset();
        return false;
    }
    return true;
}

bool CIABuilder::AddContent(u16 content_id, NCCHContainer& ncch) {
    file->Seek(written, SEEK_SET); // To enforce alignment
    file->SetHashEnabled(true);

    {
        std::lock_guard lock{abort_ncch_mutex};
        abort_ncch = &ncch;
    }

    const auto ret = ncch.DecryptToFile(file, [this](std::size_t current, std::size_t total) {
        callback(written + current, total_size);
    });

    {
        std::lock_guard lock{abort_ncch_mutex};
        abort_ncch = nullptr;
    }

    if (ret != ResultStatus::Success) {
        file.reset();
        return false;
    }
    written = Common::AlignUp(file->Tell(), CIA_ALIGNMENT);

    header.content_size = written - content_offset;

    auto& tmd_chunk = tmd.GetContentChunkByID(content_id);
    header.SetContentPresent(tmd_chunk.index);
    file->GetHash(tmd_chunk.hash.data());
    file->SetHashEnabled(false);

    // DLCs do not have a meta
    if (tmd_chunk.index != TMDContentIndex::Main || (tmd.GetTitleID() >> 32) == 0x0004008c) {
        return true;
    }

    // Load meta if the content is main
    header.meta_size = sizeof(meta);

    static_assert(sizeof(ncch.exheader_header.dependency_list) == sizeof(meta.dependencies),
                  "Dependency list should be of the same size in NCCH and CIA");
    std::memcpy(meta.dependencies.data(), &ncch.exheader_header.dependency_list,
                sizeof(meta.dependencies));

    meta.core_version = ncch.exheader_header.arm11_system_local_caps.core_version;

    std::vector<u8> smdh_buffer;
    if (ncch.LoadSectionExeFS("icon", smdh_buffer) != ResultStatus::Success) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS");
        return true;
    }
    std::memcpy(meta.icon_data.data(), smdh_buffer.data(),
                std::min(meta.icon_data.size(), smdh_buffer.size()));

    return true;
}

bool CIABuilder::Finalize() {
    // Write header
    file->Seek(0, SEEK_SET);
    if (file->WriteBytes(&header, sizeof(header)) != sizeof(header)) {
        LOG_ERROR(Core, "Failed to write header");
        file.reset();
        return false;
    }

    // Write TMD
    file->Seek(tmd_offset, SEEK_SET);
    if (tmd.Save(*file) != ResultStatus::Success) {
        file.reset();
        return false;
    }

    // Write meta
    if (header.meta_size) {
        file->Seek(written, SEEK_SET);
        if (file->WriteBytes(&meta, sizeof(meta)) != sizeof(meta)) {
            LOG_ERROR(Core, "Failed to write meta");
            file.reset();
            return false;
        }
    }

    callback(total_size, total_size);
    file.reset();
    return true;
}

void CIABuilder::Abort() {
    std::lock_guard lock{abort_ncch_mutex};
    if (abort_ncch) {
        abort_ncch->AbortDecryptToFile();
    }
}

} // namespace Core
