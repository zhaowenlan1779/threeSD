// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include "common/common_types.h"
#include "common/thread.h"

namespace Core {

class QuickDecryptor {
public:
    /// (current_size, total_size)
    using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

    /**
     * Initializes the decryptor.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
     */
    explicit QuickDecryptor(const std::string& root_folder);

    ~QuickDecryptor();

    bool DecryptAndWriteFile(const std::string& source, const std::string& destination,
                             const ProgressCallback& callback = [](std::size_t, std::size_t) {});

    void DataReadLoop();
    void DataDecryptLoop();
    void DataWriteLoop();

    void Abort();

    /// Reset the imported_size counter for this content and set a new total_size.
    void Reset(std::size_t total_size);

private:
    static constexpr std::size_t BufferSize = 16 * 1024; // 16 KB

    std::string root_folder;
    std::string source;
    std::string destination;

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

    ProgressCallback callback;

    Common::Event completion_event;
    bool is_good{true};
    std::atomic_bool is_running{false};
};

} // namespace Core
