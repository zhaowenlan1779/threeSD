// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/data/inner_fat.hpp"

namespace Core {

class Savegame final : public Archive<Savegame> {
public:
    /// @param data Data of the DISA archive
    explicit Savegame(std::vector<u8> data);
    ~Savegame();

    bool IsGood() const;
    bool Extract(std::string path) const;

private:
    bool Init(std::vector<u8> data);
    bool CheckMagic() const;
    bool ExtractFile(const std::string& path, std::size_t index) const;
    ArchiveFormatInfo GetFormatInfo() const;

    bool is_good = false;

    friend class Archive<Savegame>;
    friend class InnerFAT<Savegame>;
    friend class SDMCImporter; // Needed to read config savegame
};

} // namespace Core
