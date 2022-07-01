// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/file_sys/data/inner_fat.hpp"

namespace Core {

class SDMCDecryptor;

class Extdata final : public Archive<Extdata> {
public:
    /**
     * Loads an SD extdata folder.
     * @param data_path Path to the ENCRYPTED SD extdata folder, relative to decryptor root
     * @param decryptor Const reference to the SDMCDecryptor.
     */
    explicit Extdata(std::string data_path, const SDMCDecryptor& decryptor);

    /**
     * Loads an Extdata folder without encryption.
     * @param data_path Path to the DECRYPTED extdata folder
     */
    explicit Extdata(std::string data_path);

    ~Extdata();

    bool IsGood() const;
    bool Extract(std::string path) const;

private:
    bool Init();
    bool CheckMagic() const;
    std::vector<u8> ReadFile(const std::string& path) const;
    bool ExtractFile(const std::string& path, u32 index) const;
    ArchiveFormatInfo GetFormatInfo() const;

    bool is_good = false;
    std::string data_path;
    const SDMCDecryptor* decryptor = nullptr;
    bool use_decryptor = true;

    friend class Archive<Extdata>;
    friend class InnerFAT<Extdata>;
};

} // namespace Core
