// Copyright 2021 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <unordered_map>
#include <cryptopp/integer.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/cia_common.h"
#include "core/file_sys/data/data_container.h"

namespace Core {

// Sizes include padding (0x34 for RSA, 0x3C for ECC)
inline std::size_t GetPublicKeySize(u32 public_key_type) {
    switch (public_key_type) {
    case PublicKeyType::RSA_4096:
        return 0x238;
    case PublicKeyType::RSA_2048:
        return 0x138;
    case PublicKeyType::ECC:
        return 0x78;
    }

    LOG_ERROR(Common_Filesystem, "Tried to read cert with bad public key {}", public_key_type);
    return 0;
}

bool Certificate::Load(std::vector<u8> file_data, std::size_t offset) {
    const auto total_size = static_cast<std::size_t>(file_data.size() - offset);

    if (!signature.Load(file_data, offset)) {
        return false;
    }
    // certificate body
    const auto signature_size = signature.GetSize();
    TRY_MEMCPY(&body, file_data, offset + signature_size, sizeof(Body));

    // Public key lengths are variable
    const auto public_key_size = GetPublicKeySize(body.key_type);
    if (public_key_size == 0) {
        return false;
    }
    public_key.resize(public_key_size);

    const auto public_key_offset = offset + signature_size + sizeof(Body);
    TRY_MEMCPY(public_key.data(), file_data, public_key_offset, public_key.size());
    return true;
}

bool Certificate::Save(FileUtil::IOFile& file) const {
    // signature
    if (!signature.Save(file)) {
        return false;
    }

    // body
    if (file.WriteBytes(&body, sizeof(body)) != sizeof(body)) {
        LOG_ERROR(Core, "Failed to write body");
        return false;
    }

    // public key
    if (file.WriteBytes(public_key.data(), public_key.size()) != public_key.size()) {
        LOG_ERROR(Core, "Failed to write public key");
        return false;
    }

    return true;
}

std::size_t Certificate::GetSize() const {
    return signature.GetSize() + sizeof(Body) + public_key.size();
}

std::pair<CryptoPP::Integer, CryptoPP::Integer> Certificate::GetRSAPublicKey() const {
    if (body.key_type == PublicKeyType::RSA_2048) {
        return {CryptoPP::Integer(public_key.data(), 0x100),
                CryptoPP::Integer(public_key.data() + 0x100, 0x4)};
    } else if (body.key_type == PublicKeyType::RSA_4096) {
        return {CryptoPP::Integer(public_key.data(), 0x200),
                CryptoPP::Integer(public_key.data() + 0x200, 0x4)};
    } else {
        UNREACHABLE_MSG("Certificate is not RSA");
    }
}

namespace Certs {

static std::unordered_map<std::string, Certificate> g_certs;
static bool g_is_loaded = false;

bool Load(const std::string& path) {
    g_certs.clear();

    FileUtil::IOFile file(path, "rb");
    DataContainer container(file.GetData());
    std::vector<std::vector<u8>> data;
    if (!container.IsGood() || !container.GetIVFCLevel4Data(data)) {
        return false;
    }

    CertsDBHeader header;
    TRY_MEMCPY(&header, data[0], 0, sizeof(header));

    if (header.magic != MakeMagic('C', 'E', 'R', 'T')) {
        LOG_ERROR(Core, "File is invalid {}", path);
        return false;
    }

    const auto total_size = header.size + sizeof(header);
    if (data[0].size() < total_size) {
        LOG_ERROR(Core, "File {} header reports invalid size, may be corrupted", path);
        return false;
    }

    std::size_t pos = sizeof(header);
    while (pos < total_size) {
        Certificate cert;
        if (!cert.Load(data[0], pos)) { // Failed to load
            return false;
        }
        pos += cert.GetSize();

        const auto issuer = Common::StringFromFixedZeroTerminatedBuffer(cert.body.issuer.data(),
                                                                        cert.body.issuer.size());
        const auto name = Common::StringFromFixedZeroTerminatedBuffer(cert.body.name.data(),
                                                                      cert.body.name.size());
        const auto full_name = issuer + "-" + name;
        g_certs.emplace(full_name, std::move(cert));
    }

    for (const auto& cert : CIACertNames) {
        if (!g_certs.count(cert)) {
            LOG_ERROR(Core, "Cert {} required for CIA building but does not exist", cert);
            return false;
        }
    }

    g_is_loaded = true;
    return true;
}

bool IsLoaded() {
    return g_is_loaded;
}

const Certificate& Get(const std::string& name) {
    return g_certs.at(name);
}

bool Exists(const std::string& name) {
    return g_certs.count(name);
}

} // namespace Certs

} // namespace Core
