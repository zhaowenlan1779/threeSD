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
#include "core/file_decryptor.h"

namespace Core {

FileDecryptor::FileDecryptor() = default;

FileDecryptor::~FileDecryptor() = default;

void FileDecryptor::SetCrypto(std::shared_ptr<CryptoFunc> crypto_) {
    crypto = std::move(crypto_);
}

bool FileDecryptor::CryptAndWriteFile(std::shared_ptr<FileUtil::IOFile> source_, std::size_t size,
                                      std::shared_ptr<FileUtil::IOFile> destination_,
                                      const Common::ProgressCallback& callback_) {
    if (is_running) {
        LOG_ERROR(Core, "Decryptor is running");
        return false;
    }

    if (size == 0) {
        return true;
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

    source = std::move(source_);
    destination = std::move(destination_);
    callback = callback_;

    current_total_size = size;

    is_good = is_running = true;

    read_thread = std::make_unique<std::thread>(&FileDecryptor::DataReadLoop, this);
    write_thread = std::make_unique<std::thread>(&FileDecryptor::DataWriteLoop, this);
    if (crypto) {
        decrypt_thread = std::make_unique<std::thread>(&FileDecryptor::DataDecryptLoop, this);
    }

    completion_event.Wait();
    is_running = false;

    read_thread->join();
    write_thread->join();
    if (crypto) {
        decrypt_thread->join();
    }

    // Release the files
    source.reset();
    destination.reset();

    bool ret = is_good;
    is_good = true;
    return ret;
}

void FileDecryptor::DataReadLoop() {
    std::size_t current_buffer = 0;
    bool is_first_run = true;

    if (!*source) {
        is_good = false;
        completion_event.Set();
        return;
    }

    std::size_t file_size = current_total_size;

    while (is_running && file_size > 0) {
        if (is_first_run) {
            if (current_buffer == buffers.size() - 1) {
                is_first_run = false;
            }
        } else {
            data_written_event[current_buffer].Wait();
        }

        const auto bytes_to_read = std::min(BufferSize, file_size);
        if (source->ReadBytes(buffers[current_buffer].data(), bytes_to_read) != bytes_to_read) {
            is_good = false;
            completion_event.Set();
            return;
        }
        file_size -= bytes_to_read;

        data_read_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }
}

void FileDecryptor::DataDecryptLoop() {
    std::size_t current_buffer = 0;
    std::size_t file_size = current_total_size;

    while (is_running && file_size > 0) {
        data_read_event[current_buffer].Wait();

        const auto bytes_to_process = std::min(BufferSize, file_size);
        crypto->ProcessData(buffers[current_buffer].data(), bytes_to_process);

        file_size -= bytes_to_process;

        data_decrypted_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }
}

void FileDecryptor::DataWriteLoop() {
    std::size_t current_buffer = 0;

    if (!*destination) {
        is_good = false;
        completion_event.Set();
        return;
    }

    std::size_t file_size = current_total_size;
    std::size_t iteration = 0;
    /// The number of iterations each progress report covers. 32 * 16K = 512K
    constexpr std::size_t ProgressReportFreq = 32;

    while (is_running && file_size > 0) {
        if (iteration % ProgressReportFreq == 0) {
            callback(imported_size, total_size);
        }

        iteration++;

        if (crypto) {
            data_decrypted_event[current_buffer].Wait();
        } else {
            data_read_event[current_buffer].Wait();
        }

        const auto bytes_to_write = std::min(BufferSize, file_size);
        if (destination->WriteBytes(buffers[current_buffer].data(), bytes_to_write) !=
            bytes_to_write) {
            is_good = false;
            completion_event.Set();
            return;
        }
        file_size -= bytes_to_write;
        imported_size += bytes_to_write;

        data_written_event[current_buffer].Set();
        current_buffer = (current_buffer + 1) % buffers.size();
    }

    completion_event.Set();
}

void FileDecryptor::Abort() {
    if (is_running.exchange(false)) {
        is_good = false;
        completion_event.Set();
    }
}

void FileDecryptor::Reset(std::size_t total_size_) {
    total_size = total_size_;
    imported_size = 0;
}

CryptoFunc::~CryptoFunc() = default;

class CryptoFunc_AES_CTR final : public CryptoFunc {
public:
    explicit CryptoFunc_AES_CTR(const Key::AESKey& key, const Key::AESKey& ctr,
                                std::size_t seek_pos = 0) {

        aes.SetKeyWithIV(key.data(), key.size(), ctr.data());
        aes.Seek(seek_pos);
    }

    ~CryptoFunc_AES_CTR() override = default;

    void ProcessData(u8* data, std::size_t size) override {
        aes.ProcessData(data, data, size);
    }

private:
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
};

std::shared_ptr<CryptoFunc> CreateCTRCrypto(const Key::AESKey& key, const Key::AESKey& ctr,
                                            std::size_t seek_pos) {
    return std::make_shared<CryptoFunc_AES_CTR>(key, ctr, seek_pos);
}

} // namespace Core
