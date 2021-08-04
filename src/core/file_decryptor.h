// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include "common/common_types.h"
#include "common/progress_callback.h"
#include "common/thread.h"
#include "core/key/key.h"

namespace Core {

class CryptoFunc;

/**
 * Generalized file decryptor.
 * Helper that reads, decrypts and writes data. This uses three threads to process the data
 * and call progress callbacks occasionally.
 */
class FileDecryptor {
public:
    explicit FileDecryptor();
    ~FileDecryptor();

    /**
     * Set up the crypto to use.
     * Default / nullptr is plain copy.
     */
    void SetCrypto(std::shared_ptr<CryptoFunc> crypto);

    /**
     * Crypts and writes a file.
     *
     * @param source Source file
     * @param size Size to read, decrypt and write
     * @param destination Destination file
     * @param callback Progress callback. default for nothing.
     */
    bool CryptAndWriteFile(
        std::shared_ptr<FileUtil::IOFile> source, std::size_t size,
        std::shared_ptr<FileUtil::IOFile> destination,
        const Common::ProgressCallback& callback = [](std::size_t, std::size_t) {});

    void DataReadLoop();
    void DataDecryptLoop();
    void DataWriteLoop();

    void Abort();

    /// Reset the imported_size counter for this content and set a new total_size.
    void Reset(std::size_t total_size);

private:
    static constexpr std::size_t BufferSize = 16 * 1024; // 16 KB

    std::shared_ptr<FileUtil::IOFile> source;
    std::shared_ptr<FileUtil::IOFile> destination;
    std::shared_ptr<CryptoFunc> crypto;

    // Total size of this content, may consist of multiple files
    std::size_t total_size{};
    // Total size of the current file to process
    std::size_t current_total_size{};
    // Total imported size for this content
    std::size_t imported_size{};

    std::array<std::array<u8, BufferSize>, 3> buffers;
    std::array<Common::Event, 3> data_read_event;
    std::array<Common::Event, 3> data_decrypted_event;
    std::array<Common::Event, 3> data_written_event;

    std::unique_ptr<std::thread> read_thread;
    std::unique_ptr<std::thread> decrypt_thread;
    std::unique_ptr<std::thread> write_thread;

    Common::ProgressCallback callback;

    Common::Event completion_event;
    bool is_good{true};
    std::atomic_bool is_running{false};
};

class CryptoFunc {
public:
    virtual ~CryptoFunc();
    virtual void ProcessData(u8* data, std::size_t size) = 0;
};

std::shared_ptr<CryptoFunc> CreateCTRCrypto(const Key::AESKey& key, const Key::AESKey& ctr,
                                            std::size_t seek_pos = 0);

} // namespace Core
