// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"

namespace CryptoPP {
class PK_MessageAccumulator;
}

namespace FileUtil {
class IOFile;
}

namespace Core {

/// Consists of a signature type, a signature, and alignment to 0x40.
class Signature {
public:
    bool Load(const std::vector<u8>& file_data, std::size_t offset = 0);

    /// Writes signature to file. Includes the alignment
    bool Save(FileUtil::IOFile& file) const;

    std::size_t GetSize() const;

    /// Verifies the signature. Accepts a functor which should add the message to the accumulator
    bool Verify(const std::string& issuer,
                const std::function<void(CryptoPP::PK_MessageAccumulator*)>& func) const;

    u32_be type;
    std::vector<u8> data;
};

} // namespace Core
