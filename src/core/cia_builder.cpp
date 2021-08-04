// Copyright 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "core/cia_builder.h"
#include "core/db/title_db.h"
#include "core/db/title_keys_bin.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/cia_common.h"
#include "core/file_sys/ticket.h"
#include "core/file_sys/title_metadata.h"
#include "core/importer.h"

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

    bool VerifyHash(u8* out) {
        return sha.Verify(out);
    }

    std::size_t Write(const char* data, std::size_t length) override {
        const std::size_t length_written = FileUtil::IOFile::Write(data, length);
        if (hash_enabled) {
            sha.Update(reinterpret_cast<const CryptoPP::byte*>(data), length_written);
        }
        return length_written;
    }

private:
    CryptoPP::SHA256 sha;
    bool hash_enabled{};
};

CIABuilder::CIABuilder() = default;
CIABuilder::~CIABuilder() = default;

bool CIABuilder::Init(CIABuildType type_, const std::string& destination, TitleMetadata tmd_,
                      const Config& config, std::size_t total_size_,
                      const Common::ProgressCallback& callback_) {

    type = type_;
    header = {};
    meta = {};

    file = std::make_shared<HashedFile>(destination, "wb");
    if (!*file) {
        LOG_ERROR(Core, "Could not open file {}", destination);
        return false;
    }

    tmd = std::move(tmd_);
    if (type == CIABuildType::Standard) {
        // Remove encrypted flag from TMD chunks
        for (auto& chunk : tmd.tmd_chunks) {
            chunk.type &= ~0x01;
        }
    }
    if (type == CIABuildType::Legit || type == CIABuildType::PirateLegit) {
        // Check for legit TMD
        if (!tmd.VerifyHashes() || !tmd.ValidateSignature()) {
            LOG_ERROR(Core, "TMD is not legit");
            return false;
        }
    }

    header.header_size = sizeof(header);
    // Header will be written in Finalize

    // Cert
    cert_offset = Common::AlignUp(header.header_size, CIA_ALIGNMENT);
    header.cert_size = CIA_CERT_SIZE;
    if (!WriteCert(config.certs_db_path)) {
        LOG_ERROR(Core, "Could not write cert to file {}", destination);
        return false;
    }

    // Ticket
    ticket_offset = Common::AlignUp(cert_offset + header.cert_size, CIA_ALIGNMENT);
    if (!WriteTicket(config.ticket_db_path, config.enc_title_keys_bin_path)) {
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
    if (!Certs::IsLoaded()) {
        return false;
    }

    file->Seek(cert_offset, SEEK_SET);
    for (const auto& cert : CIACertNames) {
        if (!Certs::Get(cert).Save(*file)) {
            LOG_ERROR(Core, "Failed to write cert {}", cert);
            return false;
        }
    }
    return true;
}

static bool FindLegitTicket(Ticket& ticket, u64 title_id, const std::string& ticket_db_path) {
    TicketDB ticket_db(ticket_db_path);
    if (ticket_db.IsGood() && ticket_db.tickets.count(title_id)) {
        ticket = ticket_db.tickets.at(title_id);
        if (!ticket.ValidateSignature()) {
            LOG_ERROR(Core, "Ticket in ticket.db for {:016x} is not legit", title_id);
            return false;
        }
        return true;
    }

    LOG_ERROR(Core, "Ticket for {:016x} does not exist in ticket.db", title_id);
    return false;
}

static Ticket BuildStandardTicket(u64 title_id, const std::string& ticket_db_path,
                                  const std::string& enc_title_keys_bin_path) {
    Ticket ticket = BuildFakeTicket(title_id);

    // Fill in common_key_index and title_key from either ticket.db (installed tickets)
    // or GM9 support files (encTitleKeys.bin) found on the SD card
    if (TicketDB ticket_db(ticket_db_path);
        ticket_db.IsGood() && ticket_db.tickets.count(title_id)) { // ticket.db

        const auto& legit_ticket = ticket_db.tickets.at(title_id);
        ticket.body.common_key_index = legit_ticket.body.common_key_index;
        ticket.body.title_key = legit_ticket.body.title_key;

    } else if (TitleKeysMap enc_title_keys;
               LoadTitleKeysBin(enc_title_keys, enc_title_keys_bin_path) &&
               enc_title_keys.count(title_id)) { // support files

        const auto& entry = enc_title_keys.at(title_id);
        ticket.body.common_key_index = entry.common_key_index;
        ticket.body.title_key = entry.title_key;
    } else {
        LOG_WARNING(Core, "Could not find title key for {:016x}", title_id);
    }
    return ticket;
}

static Key::AESKey GetTitleKey(const Ticket& ticket) {
    Key::SelectCommonKeyIndex(ticket.body.common_key_index);
    if (!Key::IsNormalKeyAvailable(Key::TicketCommonKey)) {
        LOG_ERROR(Core, "Ticket common key is not available");
        return {};
    }

    const auto ticket_key = Key::GetNormalKey(Key::TicketCommonKey);
    Key::AESKey ctr{};
    std::memcpy(ctr.data(), &ticket.body.title_id, 8);

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption aes;
    aes.SetKeyWithIV(ticket_key.data(), ticket_key.size(), ctr.data());

    Key::AESKey title_key = ticket.body.title_key;
    aes.ProcessData(title_key.data(), title_key.data(), title_key.size());
    return title_key;
}

bool CIABuilder::WriteTicket(const std::string& ticket_db_path,
                             const std::string& enc_title_keys_bin_path) {

    const auto title_id = tmd.GetTitleID();

    Ticket ticket;
    if (type == CIABuildType::Legit) {
        if (!FindLegitTicket(ticket, title_id, ticket_db_path)) {
            return false;
        }
    } else {
        ticket = BuildStandardTicket(title_id, ticket_db_path, enc_title_keys_bin_path);
    }
    title_key = GetTitleKey(ticket);

    header.tik_size = ticket.GetSize();

    file->Seek(ticket_offset, SEEK_SET);
    if (!ticket.Save(*file)) {
        LOG_ERROR(Core, "Could not write ticket");
        return false;
    }
    return true;
}

class CIAEncryptAndHash final : public CryptoFunc {
public:
    explicit CIAEncryptAndHash(const Key::AESKey& key, const Key::AESKey& iv) {
        aes.SetKeyWithIV(key.data(), key.size(), iv.data());
    }

    ~CIAEncryptAndHash() override = default;

    void ProcessData(u8* data, std::size_t size) override {
        sha.Update(data, size);
        aes.ProcessData(data, data, size);
    }

    bool VerifyHash(const u8* hash) {
        return sha.Verify(hash);
    }

private:
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption aes;
    CryptoPP::SHA256 sha;
};

bool CIABuilder::AddContent(u16 content_id, NCCHContainer& ncch) {
    if (!ncch.Load()) {
        return false;
    }

    file->Seek(written, SEEK_SET); // To enforce alignment

    auto& tmd_chunk = tmd.GetContentChunkByID(content_id);
    const auto progress_callback = [this](std::size_t current, std::size_t total) {
        callback(written + current, total_size);
    };
    if (type == CIABuildType::Standard) {
        // Decrypt the NCCH. We created a HashedFile to transparently calculate the hash as there
        // is no easy way to get decrypted NCCH content otherwise.
        file->SetHashEnabled(true);
        {
            std::lock_guard lock{abort_ncch_mutex};
            abort_ncch = &ncch;
        }
        const auto ret = ncch.DecryptToFile(file, progress_callback);
        {
            std::lock_guard lock{abort_ncch_mutex};
            abort_ncch = nullptr;
        }

        if (!ret) {
            return false;
        }
        file->GetHash(tmd_chunk.hash.data());
        file->SetHashEnabled(false);
    } else {
        ncch.file->Seek(0, SEEK_SET);

        // Calculate IV
        Key::AESKey iv{};
        std::memcpy(iv.data(), &tmd_chunk.index, sizeof(tmd_chunk.index));

        const bool is_encrypted = static_cast<u16>(tmd_chunk.type) & 0x01;

        // For encrypted content, the hashes are calculated before CIA/CDN encryption.
        // So we have to add hash calculation to the CryptoFunc of the FileDecryptor.
        // For unencrypted content, we can just use HashedFile's hashing.
        std::shared_ptr<CIAEncryptAndHash> crypto;
        if (is_encrypted) {
            crypto = std::make_shared<CIAEncryptAndHash>(title_key, iv);
        } else { // crypto left to be null
            file->SetHashEnabled(true);
        }
        decryptor.SetCrypto(crypto);
        if (!decryptor.CryptAndWriteFile(ncch.file, ncch.file->GetSize(), file,
                                         progress_callback)) {

            return false;
        }

        // Verify the hash
        bool verified{};
        if (is_encrypted) {
            verified = crypto->VerifyHash(tmd_chunk.hash.data());
        } else {
            verified = file->VerifyHash(tmd_chunk.hash.data());
            file->SetHashEnabled(false);
        }
        if (!verified) {
            LOG_ERROR(Core, "Hash dismatch for content {}", content_id);
            return false;
        }
    }

    written = Common::AlignUp(file->Tell(), CIA_ALIGNMENT);

    header.content_size = written - content_offset;
    header.SetContentPresent(tmd_chunk.index);

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
    if (!ncch.LoadSectionExeFS("icon", smdh_buffer)) {
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
        return false;
    }

    // Write TMD
    if (type == CIABuildType::Standard) {
        tmd.FixHashes();
    }
    file->Seek(tmd_offset, SEEK_SET);
    if (!tmd.Save(*file)) {
        return false;
    }

    // Write meta
    if (header.meta_size) {
        file->Seek(written, SEEK_SET);
        if (file->WriteBytes(&meta, sizeof(meta)) != sizeof(meta)) {
            LOG_ERROR(Core, "Failed to write meta");
            return false;
        }
    }

    callback(total_size, total_size);
    return true;
}

void CIABuilder::Abort() {
    if (type == CIABuildType::Standard) { // Abort NCCH decryption
        std::lock_guard lock{abort_ncch_mutex};
        if (abort_ncch) {
            abort_ncch->AbortDecryptToFile();
        }
    } else { // Abort the decryptor
        decryptor.Abort();
    }
}

} // namespace Core
