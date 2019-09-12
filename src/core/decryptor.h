// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"
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

private:
    std::string root_folder;
    QuickDecryptor quick_decryptor;
};

} // namespace Core
