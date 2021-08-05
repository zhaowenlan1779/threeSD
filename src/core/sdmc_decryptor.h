// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/file_decryptor.h"

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
    bool DecryptAndWriteFile(
        const std::string& source, const std::string& destination,
        const Common::ProgressCallback& callback = [](u64, u64) {});

    void Abort();

    /**
     * Decrypts a file and reads it into a vector.
     * @param source Path to the file relative to the root folder, starting with "/".
     */
    std::vector<u8> DecryptFile(const std::string& source) const;

private:
    std::string root_folder;
    FileDecryptor file_decryptor;
};

/// Interface for reading an SDMC file like a normal IOFile. This is read-only.
class SDMCFile : public FileUtil::IOFile {
public:
    SDMCFile(std::string root_folder, const std::string& filename, const char openmode[],
             int flags = 0);

    ~SDMCFile() override;

    std::size_t Read(char* data, std::size_t length) override;
    std::size_t Write(const char* data, std::size_t length) override;
    bool Seek(s64 off, int origin) override;

private:
    void DecryptData(u8* data, std::size_t size);

    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Core
