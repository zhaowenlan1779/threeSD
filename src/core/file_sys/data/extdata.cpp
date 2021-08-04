// Copyright 2021 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/decryptor.h"
#include "core/file_sys/data/data_container.h"
#include "core/file_sys/data/extdata.h"

namespace Core {

Extdata::Extdata(std::string data_path_, const SDMCDecryptor& decryptor_)
    : data_path(std::move(data_path_)), decryptor(&decryptor_) {

    if (data_path.back() != '/' && data_path.back() != '\\') {
        data_path += '/';
    }

    use_decryptor = true;
    is_good = Init();
}

Extdata::Extdata(std::string data_path_) : data_path(std::move(data_path_)) {
    if (data_path.back() != '/' && data_path.back() != '\\') {
        data_path += '/';
    }

    use_decryptor = false;
    is_good = Init();
}

Extdata::~Extdata() = default;

bool Extdata::CheckMagic() const {
    if (header.fat_header.magic != MakeMagic('V', 'S', 'X', 'E') ||
        header.fat_header.version != 0x30000) {

        LOG_ERROR(Core, "File is invalid, decryption errors may have happened.");
        return false;
    }
    return true;
}

bool Extdata::IsGood() const {
    return is_good;
}

bool Extdata::Extract(std::string path) const {
    if (path.back() != '/' && path.back() != '\\') {
        path += '/';
    }

    if (!ExtractDirectory(path, 1)) {
        return false;
    }

    // Write format info
    const auto format_info = GetFormatInfo();
    return FileUtil::WriteBytesToFile(path + "metadata", &format_info, sizeof(format_info));
}

std::vector<u8> Extdata::ReadFile(const std::string& path) const {
    if (use_decryptor) {
        return decryptor->DecryptFile(path);
    } else {
        FileUtil::IOFile file(path, "rb");
        return file.GetData();
    }
}

bool Extdata::Init() {
    // Read VSXE file
    auto vsxe_raw = ReadFile(data_path + "00000000/00000001");
    if (vsxe_raw.empty()) {
        LOG_ERROR(Core, "Failed to load or decrypt VSXE");
        return false;
    }

    DataContainer vsxe_container(std::move(vsxe_raw));
    if (!vsxe_container.IsGood()) {
        return false;
    }

    std::vector<std::vector<u8>> data;
    if (!vsxe_container.GetIVFCLevel4Data(data)) {
        return false;
    }

    return Archive<Extdata>::Init(std::move(data));
}

bool Extdata::ExtractFile(const std::string& path, std::size_t index) const {
    /// Maximum amount of device files a device directory can hold.
    constexpr u32 DeviceDirCapacity = 126;

    u32 file_index = index + 1;
    u32 sub_directory_id = file_index / DeviceDirCapacity;
    u32 sub_file_id = file_index % DeviceDirCapacity;
    std::string device_file_path =
        fmt::format("{}{:08x}/{:08x}", data_path, sub_directory_id, sub_file_id);

    auto container_data = ReadFile(device_file_path);
    if (container_data.empty()) { // File does not exist?
        LOG_WARNING(Core, "Ignoring file {}", device_file_path);
        return true;
    }

    DataContainer container(std::move(container_data));
    if (!container.IsGood()) {
        return false;
    }

    std::vector<std::vector<u8>> data;
    if (!container.GetIVFCLevel4Data(data)) {
        return false;
    }

    return FileUtil::WriteBytesToFile(path, data[0].data(), data[0].size());
}

ArchiveFormatInfo Extdata::GetFormatInfo() const {
    // This information is based on how Citra created the metadata in FS
    ArchiveFormatInfo format_info = {/* total_size */ 0,
                                     /* number_directories */ fs_info.maximum_directory_count,
                                     /* number_files */ fs_info.maximum_file_count,
                                     /* duplicate_data */ false};

    return format_info;
}

} // namespace Core
