// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/rsa.h>
#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/signature.h"

namespace Core {

enum SignatureType : u32 {
    Rsa4096Sha1 = 0x10000,
    Rsa2048Sha1 = 0x10001,
    EllipticSha1 = 0x10002,
    Rsa4096Sha256 = 0x10003,
    Rsa2048Sha256 = 0x10004,
    EcdsaSha256 = 0x10005
};

static u32 GetSignatureSize(u32 type) {
    switch (type) {
    case Rsa4096Sha1:
    case Rsa4096Sha256:
        return 0x200;

    case Rsa2048Sha1:
    case Rsa2048Sha256:
        return 0x100;

    case EllipticSha1:
    case EcdsaSha256:
        return 0x3C;
    }

    LOG_ERROR(Common_Filesystem, "Invalid signature type {}", type);
    return 0;
}

bool Signature::Load(const std::vector<u8>& file_data, std::size_t offset) {
    TRY_MEMCPY(&type, file_data, offset, sizeof(type));

    const auto data_size = GetSignatureSize(type);
    if (data_size == 0) {
        return false;
    }

    data.resize(data_size);
    TRY_MEMCPY(data.data(), file_data, offset + sizeof(u32), data_size);
    return true;
}

bool Signature::Save(FileUtil::IOFile& file) const {
    if (file.WriteBytes(&type, sizeof(type)) != sizeof(type)) {
        LOG_ERROR(Core, "Could not write to file");
        return false;
    }
    if (file.WriteBytes(data.data(), data.size()) != data.size()) {
        LOG_ERROR(Core, "Could not write to file");
        return false;
    }
    return file.Seek(GetSize() - data.size() - sizeof(type), SEEK_CUR);
}

std::size_t Signature::GetSize() const {
    return Common::AlignUp(data.size() + sizeof(type), 0x40);
}

bool Signature::Verify(const std::string& issuer,
                       const std::function<void(CryptoPP::PK_MessageAccumulator*)>& func) const {

    const auto& cert = Certs::Get(issuer);
    if (type != SignatureType::Rsa2048Sha256 || cert.body.key_type != PublicKeyType::RSA_2048) {

        LOG_ERROR(Core, "Unsupported signature type or cert public key type");
        return false;
    }

    const auto [modulus, exponent] = cert.GetRSAPublicKey();
    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::Verifier verifier(modulus, exponent);

    auto* message = verifier.NewVerificationAccumulator();
    func(message);
    verifier.InputSignature(*message, data.data(), data.size());
    return verifier.Verify(message);
}

} // namespace Core
