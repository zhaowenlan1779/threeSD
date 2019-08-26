// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "core/decryptor.h"
#include "core/importer.h"
#include "core/inner_fat.h"
#include "core/key/key.h"

SDMCImporter::SDMCImporter(const Config& config_) : config(config_) {
    is_good = Init();
}

SDMCImporter::~SDMCImporter() = default;

bool SDMCImporter::Init() {
    ASSERT_MSG(config.is_good && !config.sdmc_path.empty() && !config.user_path.empty() &&
                   !config.bootrom_path.empty() && !config.movable_sed_path.empty(),
               "Config is not good");

    // Fix paths
    if (config.sdmc_path.back() != '/' && config.sdmc_path.back() != '\\') {
        config.sdmc_path += '/';
    }

    if (config.user_path.back() != '/' && config.user_path.back() != '\\') {
        config.user_path += '/';
    }

    Key::ClearKeys();
    Key::LoadBootromKeys(config.bootrom_path);
    Key::LoadMovableSedKeys(config.movable_sed_path);

    if (!Key::IsNormalKeyAvailable(Key::SDKey)) {
        LOG_ERROR(Core, "SDKey is not available");
        return false;
    }

    decryptor = std::make_unique<SDMCDecryptor>(config.sdmc_path);

    FileUtil::SetUserPath(config.user_path);
    return true;
}

bool SDMCImporter::IsGood() const {
    return is_good;
}

bool SDMCImporter::ImportContent(const ContentSpecifier& specifier) {
    switch (specifier.type) {
    case ContentType::Application:
    case ContentType::Update:
    case ContentType::DLC:
        return ImportTitle(specifier.id);
    case ContentType::Savegame:
        return ImportSavegame(specifier.id);
    case ContentType::Extdata:
        return ImportExtdata(specifier.id);
    case ContentType::Sysdata:
        return ImportSysdata(specifier.id);
    default:
        UNREACHABLE();
    }
}

bool SDMCImporter::ImportTitle(u64 id) {
    const auto path = fmt::format("title/{:08x}/{:08x}/content/", (id >> 32), (id & 0xFFFFFFFF));
    return FileUtil::ForeachDirectoryEntry(
        nullptr, config.sdmc_path + path,
        [this, &path](u64* /*num_entries_out*/, const std::string& directory,
                      const std::string& virtual_name) {
            if (FileUtil::IsDirectory(directory + virtual_name)) {
                return true;
            }
            return decryptor->DecryptAndWriteFile(
                "/" + path + virtual_name,
                FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) + path + virtual_name);
        });
}

bool SDMCImporter::ImportSavegame(u64 id) {
    const auto path = fmt::format("title/{:08x}/{:08x}/data/", (id >> 32), (id & 0xFFFFFFFF));
    SDSavegame save(decryptor->DecryptFile(fmt::format("/{}00000001.sav", path)));
    if (!save.IsGood()) {
        return false;
    }

    return save.Extract(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) + path);
}

bool SDMCImporter::ImportExtdata(u64 id) {
    const auto path = fmt::format("extdata/{:08x}/{:08x}/", (id >> 32), (id & 0xFFFFFFFF));
    SDExtdata extdata("/" + path, *decryptor);
    if (!extdata.IsGood()) {
        return false;
    }

    return extdata.Extract(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) + path);
}

bool SDMCImporter::ImportSysdata(u64 id) {
    switch (id) {
    case 0: { // boot9.bin
        const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + BOOTROM9;
        LOG_INFO(Core, "Copying {} from {} to {}", BOOTROM9, config.bootrom_path, target_path);
        return FileUtil::Copy(config.bootrom_path, target_path);
    }
    case 1: { // safe mode firm
        // Our GM9 script dumps to different folders for different version (new/old)
        std::string real_path;
        bool is_new_3ds = false;
        if (FileUtil::Exists(config.safe_mode_firm_path + "new/")) {
            real_path = config.safe_mode_firm_path + "new/";
            is_new_3ds = true;
        } else {
            real_path = config.safe_mode_firm_path + "old/";
        }
        return FileUtil::ForeachDirectoryEntry(
            nullptr, config.safe_mode_firm_path,
            [is_new_3ds](u64* /*num_entries_out*/, const std::string& directory,
                         const std::string& virtual_name) {
                if (FileUtil::IsDirectory(directory + virtual_name)) {
                    return true;
                }
                return FileUtil::Copy(
                    directory + virtual_name,
                    fmt::format("{}title/00040138/{}/content/{}",
                                FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                                (is_new_3ds ? "20000003" : "00000003"), virtual_name));
            });
    }
    case 2: { // seed db
        const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SEED_DB;
        LOG_INFO(Core, "Copying {} from {} to {}", SEED_DB, config.seed_db_path, target_path);
        return FileUtil::Copy(config.seed_db_path, target_path);
    }
    case 3: { // secret sector
        const auto target_path =
            FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SECRET_SECTOR;
        LOG_INFO(Core, "Copying {} from {} to {}", SECRET_SECTOR, config.secret_sector_path,
                 target_path);
        return FileUtil::Copy(config.secret_sector_path, target_path);
    }
    default:
        UNREACHABLE_MSG("Unexpected sysdata id {}", id);
    }
}

std::vector<ContentSpecifier> SDMCImporter::ListContent() const {
    std::vector<ContentSpecifier> content_list;
    ListTitle(content_list);
    ListExtdata(content_list);
    ListSysdata(content_list);
    return content_list;
}

void SDMCImporter::ListTitle(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [&out, &sdmc_path = config.sdmc_path](ContentType type,
                                                                        u64 high_id) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, fmt::format("{}title/{:08x}/", sdmc_path, high_id),
            [type, high_id, &out](u64* /*num_entries_out*/, const std::string& directory,
                                  const std::string& virtual_name) {
                if (!FileUtil::IsDirectory(directory + virtual_name)) {
                    return true;
                }

                const u64 id = (high_id << 32) + std::stoull(virtual_name, nullptr, 16);
                const auto citra_path = fmt::format(
                    "{}title/{:08x}/{}/", FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir),
                    high_id, virtual_name);
                if (FileUtil::Exists(directory + virtual_name + "/content/")) {
                    out.push_back({type, id, FileUtil::Exists(citra_path + "content/")});
                }

                if (type != ContentType::Application) {
                    return true;
                }
                if (FileUtil::Exists(directory + virtual_name + "/data/")) {
                    out.push_back(
                        {ContentType::Savegame, id, FileUtil::Exists(citra_path + "data/")});
                }
                return true;
            });
    };

    ProcessDirectory(ContentType::Application, 0x00040000);
    ProcessDirectory(ContentType::Update, 0x0004000e);
    ProcessDirectory(ContentType::DLC, 0x0004008c);
}

void SDMCImporter::ListExtdata(std::vector<ContentSpecifier>& out) const {
    FileUtil::ForeachDirectoryEntry(
        nullptr, fmt::format("{}extdata/00000000/", config.sdmc_path),
        [&out](u64* /*num_entries_out*/, const std::string& directory,
               const std::string& virtual_name) {
            if (!FileUtil::IsDirectory(directory + virtual_name)) {
                return true;
            }

            const u64 id = std::stoull(virtual_name, nullptr, 16);
            const auto citra_path =
                fmt::format("{}extdata/00000000/{}",
                            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), virtual_name);
            out.push_back({ContentType::Extdata, id, FileUtil::Exists(citra_path)});
            return true;
        });
}

void SDMCImporter::ListSysdata(std::vector<ContentSpecifier>& out) const {
#define CHECK_CONTENT(id, var_path, citra_path)                                                    \
    if (!var_path.empty()) {                                                                       \
        out.push_back({ContentType::Sysdata, id, FileUtil::Exists(citra_path)});                   \
    }

    {
        const auto sysdata_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir);
        CHECK_CONTENT(0, config.bootrom_path, sysdata_path + BOOTROM9);
        CHECK_CONTENT(2, config.seed_db_path, sysdata_path + SEED_DB);
        CHECK_CONTENT(3, config.secret_sector_path, sysdata_path + SECRET_SECTOR);
    }

    do {
        if (config.safe_mode_firm_path.empty()) {
            break;
        }

        bool is_new = false;
        if (FileUtil::Exists(config.safe_mode_firm_path + "new/")) {
            is_new = true;
        }
        if (!is_new && !FileUtil::Exists(config.safe_mode_firm_path + "old/")) {
            LOG_ERROR(Core, "Safe mode firm path specified but not found");
            break;
        }

        const auto citra_path = fmt::format("{}title/00040138/{}/content/",
                                            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                                            (is_new ? "20000003" : "00000003"));
        CHECK_CONTENT(1, config.safe_mode_firm_path, citra_path);
    } while (0);

#undef CHECK_CONTENT
}
