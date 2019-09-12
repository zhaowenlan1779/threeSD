// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

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
#include "core/quick_decryptor.h"

namespace Core {

QuickDecryptor::QuickDecryptor(const std::string& root_folder_) : root_folder(root_folder_) {
    ASSERT_MSG(Key::IsNormalKeyAvailable(Key::SDKey),
               "SD Key must be available in order to decrypt");

    if (root_folder.back() == '/' || root_folder.back() == '\\') {
        // Remove '/' or '\' character at the end as we will add them back when combining path
        root_folder.erase(root_folder.size() - 1);
    }
}

QuickDecryptor::~QuickDecryptor() = default;

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

bool QuickDecryptor::DecryptAndWriteFile(const std::string& source_,
                                         const std::string& destination_,
                                         const ProgressCallback& callback_) {
    if (is_running) {
        LOG_ERROR(Core, "Decryptor is running");
        return false;
    }

    if (!FileUtil::CreateFullPath(destination_)) {
        LOG_ERROR(Core, "Could not create path {}", destination_);
        return false;
    }

    for (auto& event : data_read_event) {
        event.Reset();
    }
    for (auto& event : data_decrypted_event) {
        event.Reset();
    }
    for (auto& event : data_written_event) {
        event.Reset();
    }
    completion_event.Reset();

    source = source_;
    destination = destination_;
    callback = callback_;

    total_size = FileUtil::GetSize(root_folder + source);
    if (total_size == 0) {
        LOG_ERROR(Core, "Could not open file {}", root_folder + source);
        return false;
    }

    is_good = is_running = true;

    read_thread = std::make_unique<std::thread>(&QuickDecryptor::DataReadLoop, this);
    decrypt_thread = std::make_unique<std::thread>(&QuickDecryptor::DataDecryptLoop, this);
    write_thread = std::make_unique<std::thread>(&QuickDecryptor::DataWriteLoop, this);

    completion_event.Wait();
    is_running = false;

    read_thread->join();
    decrypt_thread->join();
    write_thread->join();

    bool ret = is_good;
    is_good = true;
    return ret;
}

void QuickDecryptor::DataReadLoop() {
    std::size_t current_buffer = 0;
    bool is_first_run = true;

    FileUtil::IOFile file(root_folder + source, "rb");
    if (!file) {
        is_good = false;
        completion_event.Set();
        return;
    }

    std::size_t file_size = total_size;

    while (is_running && file_size > 0) {
        if (is_first_run) {
            if (current_buffer == buffers.size() - 1) {
                is_first_run = false;
            }
        } else {
            data_written_event[current_buffer].Wait();
        }

        const auto bytes_to_read = std::min(BufferSize, file_size);
        if (file.ReadBytes(buffers[current_buffer].data(), bytes_to_read) != bytes_to_read) {
            is_good = false;
            completion_event.Set();
            return;
        }
        file_size -= bytes_to_read;

        data_read_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }
}

void QuickDecryptor::DataDecryptLoop() {
    auto ctr = GetFileCTR(source);
    auto key = Key::GetNormalKey(Key::SDKey);
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    aes.SetKeyWithIV(key.data(), key.size(), ctr.data());

    std::size_t current_buffer = 0;
    std::size_t file_size = total_size;

    while (is_running && file_size > 0) {
        data_read_event[current_buffer].Wait();

        const auto bytes_to_process = std::min(BufferSize, file_size);
        aes.ProcessData(buffers[current_buffer].data(), buffers[current_buffer].data(),
                        bytes_to_process);

        file_size -= bytes_to_process;

        data_decrypted_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }
}

void QuickDecryptor::DataWriteLoop() {
    std::size_t current_buffer = 0;

    FileUtil::IOFile file(destination, "wb");
    if (!file) {
        is_good = false;
        completion_event.Set();
        return;
    }

    std::size_t file_size = total_size;
    std::size_t iteration = 0;
    /// The number of iterations each progress report covers. 32 * 16K = 512K
    constexpr std::size_t ProgressReportFreq = 32;

    while (is_running && file_size > 0) {
        iteration++;
        if (iteration % ProgressReportFreq == 0) {
            callback(iteration * BufferSize, total_size);
        }

        data_decrypted_event[current_buffer].Wait();

        const auto bytes_to_write = std::min(BufferSize, file_size);
        if (file.WriteBytes(buffers[current_buffer].data(), bytes_to_write) != bytes_to_write) {
            is_good = false;
            completion_event.Set();
            return;
        }
        file_size -= bytes_to_write;

        data_written_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }

    completion_event.Set();
}

void QuickDecryptor::Abort() {
    if (is_running.exchange(false)) {
        is_good = false;
        completion_event.Set();
    }
}

} // namespace Core
