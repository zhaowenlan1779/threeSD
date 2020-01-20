// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/quick_decryptor.h"

namespace Core {

class SDMCDecryptor {
public:
    /**
     * Initializes the decryptor.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
     */
    explicit SDMCDecryptor(const std::string& root_folder);

    ~SDMCDecryptor();

    /**
     * Decrypts a file from the SD card and writes it into another file.
     * This function blocks, but can be aborted with the Abort() function (would return false)
     *
     * @param source Path to the file relative to the root folder, starting with "/".
     * @param destination Path to the destination file.
     * @return true on success, false otherwise
     */
    bool DecryptAndWriteFile(const std::string& source, const std::string& destination,
                             const QuickDecryptor::ProgressCallback& callback = [](std::size_t,
                                                                                   std::size_t) {});

    void Abort();

    /**
     * Decrypts a file and reads it into a vector.
     * @param source Path to the file relative to the root folder, starting with "/".
     */
    std::vector<u8> DecryptFile(const std::string& source) const;

    /**
     * Marks the beginning of a new content, resetting imported_size counter, and setting an new
     * total_size for the next content.
     * This doesn't affect at all how the contents will be imported, but will make sure the callback
     * is properly invoked.
     */
    void Reset(std::size_t total_size);

private:
    std::string root_folder;
    QuickDecryptor quick_decryptor;
};

/// Interface for reading an SDMC file like a normal IOFile. This is read-only.
class SDMCFile : public NonCopyable {
public:
    SDMCFile();

    SDMCFile(std::string root_folder, const std::string& filename, const char openmode[],
             int flags = 0);

    ~SDMCFile();

    SDMCFile(SDMCFile&& other);
    SDMCFile& operator=(SDMCFile&& other);

    void Swap(SDMCFile& other);

    bool Open(const std::string& filename, const char openmode[], int flags = 0);
    bool Close();

    template <typename T>
    std::size_t ReadArray(T* data, std::size_t length) {
        std::size_t items_read = file.ReadArray(data, length);

        if (IsGood()) {
            LOG_CRITICAL(Core, "Decrypting data...");
            DecryptData(reinterpret_cast<u8*>(data), sizeof(T) * length);
        }

        return items_read;
    }

    template <typename T>
    std::size_t ReadBytes(T* data, std::size_t length) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return ReadArray(reinterpret_cast<char*>(data), length);
    }

    bool IsOpen() const {
        return file.IsOpen();
    }

    // m_good is set to false when a read, write or other function fails
    bool IsGood() const {
        return file.IsGood();
    }
    explicit operator bool() const {
        return IsGood();
    }

    bool Seek(s64 off, int origin);
    u64 Tell() const;
    u64 GetSize() const;

    void Clear();

private:
    void DecryptData(u8* data, std::size_t size);

    FileUtil::IOFile file;
    // CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption aes;
    std::array<u8, 16> original_ctr;
    std::array<u8, 16> key;
};

} // namespace Core
