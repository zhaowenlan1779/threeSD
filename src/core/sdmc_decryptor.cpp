// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <array>
#include <vector>
#include <cryptopp/aes.h>
#include <cryptopp/files.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/assert.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/key/key.h"
#include "core/sdmc_decryptor.h"

namespace Core {

SDMCDecryptor::SDMCDecryptor(const std::string& root_folder_) : root_folder(root_folder_) {

    ASSERT_MSG(Key::IsNormalKeyAvailable(Key::SDKey),
               "SD Key must be available in order to decrypt");

    if (root_folder.back() == '/' || root_folder.back() == '\\') {
        // Remove '/' or '\' character at the end as we will add them back when combining path
        root_folder.erase(root_folder.size() - 1);
    }
}

SDMCDecryptor::~SDMCDecryptor() = default;

namespace {
std::array<u8, 16> GetFileCTR(const std::string& path) {
    auto path_utf16 = Common::UTF8ToUTF16(path);
    std::vector<u8> path_data(path_utf16.size() * 2 + 2, 0); // Add the '\0' character
    std::memcpy(path_data.data(), path_utf16.data(), path_utf16.size() * 2);

    CryptoPP::SHA256 sha;
    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> hash;
    sha.CalculateDigest(hash.data(), path_data.data(), path_data.size());

    std::array<u8, 16> ctr;
    for (int i = 0; i < 16; i++) {
        ctr[i] = hash[i] ^ hash[16 + i];
    }
    return ctr;
}
} // namespace

bool SDMCDecryptor::DecryptAndWriteFile(const std::string& source, const std::string& destination,
                                        const Common::ProgressCallback& callback) {
    if (!FileUtil::CreateFullPath(destination)) {
        LOG_ERROR(Core, "Could not create path {}", destination);
        return false;
    }

    auto key = Key::GetNormalKey(Key::SDKey);
    auto ctr = GetFileCTR(source);
    file_decryptor.SetCrypto(CreateCTRCrypto(key, ctr));

    auto source_file = std::make_shared<FileUtil::IOFile>(root_folder + source, "rb");
    auto size = source_file->GetSize();
    auto destination_file = std::make_shared<FileUtil::IOFile>(destination, "wb");
    return file_decryptor.CryptAndWriteFile(std::move(source_file), size,
                                            std::move(destination_file), callback);
}

void SDMCDecryptor::Abort() {
    file_decryptor.Abort();
}

std::vector<u8> SDMCDecryptor::DecryptFile(const std::string& source) const {
    auto ctr = GetFileCTR(source);
    auto key = Key::GetNormalKey(Key::SDKey);
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    aes.SetKeyWithIV(key.data(), key.size(), ctr.data());

    FileUtil::IOFile file(root_folder + source, "rb");
    std::vector<u8> encrypted_data = file.GetData();
    if (encrypted_data.empty()) {
        LOG_ERROR(Core, "Failed to read from {}", root_folder + source);
        return {};
    }

    std::vector<u8> data(file.GetSize());
    aes.ProcessData(data.data(), encrypted_data.data(), encrypted_data.size());
    return data;
}

struct SDMCFile::Impl {
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    std::array<u8, 16> original_ctr;
    std::array<u8, 16> key;
};

SDMCFile::SDMCFile(std::string root_folder, const std::string& filename, const char openmode[],
                   int flags) {

    impl = std::make_unique<Impl>();

    if (root_folder.back() == '/' || root_folder.back() == '\\') {
        // Remove '/' or '\' character at the end as we will add them back when combining path
        root_folder.erase(root_folder.size() - 1);
    }

    impl->original_ctr = GetFileCTR(filename);
    impl->key = Key::GetNormalKey(Key::SDKey);
    impl->aes.SetKeyWithIV(impl->key.data(), impl->key.size(), impl->original_ctr.data());

    Open(root_folder + filename, openmode, flags);
}

SDMCFile::~SDMCFile() {
    Close();
}

std::size_t SDMCFile::Read(char* data, std::size_t length) {
    const std::size_t length_read = FileUtil::IOFile::Read(data, length);
    DecryptData(reinterpret_cast<u8*>(data), length_read);
    return length_read;
}

std::size_t SDMCFile::Write(const char* data, std::size_t length) {
    UNREACHABLE_MSG("Cannot write to a SDMCFile");
}

bool SDMCFile::Seek(s64 off, int origin) {
    if (!FileUtil::IOFile::Seek(off, origin)) {
        return false;
    }
    impl->aes.Seek(Tell());
    return true;
}

void SDMCFile::DecryptData(u8* data, std::size_t size) {
    impl->aes.ProcessData(data, data, size);
}

} // namespace Core
