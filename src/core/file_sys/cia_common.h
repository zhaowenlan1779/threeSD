// Copyright 2018 Citra Emulator Project / 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "common/common_types.h"

namespace Core {

/// Full names of the certificates contained in a CIA.
constexpr std::array<const char*, 3> CIACertNames{{
    "Root-CA00000003",
    "Root-CA00000003-XS0000000c",
    "Root-CA00000003-CP0000000b",
}};

enum class CIABuildType {
    Standard,    /// Decrypted CIA with generalized ticket
    PirateLegit, /// Uses legit TMD and encryption, but with generalized ticket
    Legit,       /// Fully legit, with personal ticket containing console ID and eshop account
};

} // namespace Core
