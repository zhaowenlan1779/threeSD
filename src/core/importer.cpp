// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <regex>
#include "common/assert.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/data_container.h"
#include "core/decryptor.h"
#include "core/importer.h"
#include "core/inner_fat.h"
#include "core/key/key.h"
#include "core/ncch/ncch_container.h"
#include "core/ncch/seed_db.h"
#include "core/ncch/smdh.h"
#include "core/ncch/title_metadata.h"

namespace Core {

SDMCImporter::SDMCImporter(const Config& config_) : config(config_) {
    is_good = Init();
}

SDMCImporter::~SDMCImporter() = default;

bool SDMCImporter::Init() {
    ASSERT_MSG(!config.sdmc_path.empty() && !config.user_path.empty() &&
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

void SDMCImporter::Abort() {
    decryptor->Abort();
}

bool SDMCImporter::ImportContent(const ContentSpecifier& specifier,
                                 const ProgressCallback& callback) {
    switch (specifier.type) {
    case ContentType::Application:
    case ContentType::Update:
    case ContentType::DLC:
        return ImportTitle(specifier, callback);
    case ContentType::Savegame:
        return ImportSavegame(specifier.id, callback);
    case ContentType::Extdata:
        return ImportExtdata(specifier.id, callback);
    case ContentType::SystemArchive:
        return ImportSystemArchive(specifier.id, callback);
    case ContentType::Sysdata:
        return ImportSysdata(specifier.id, callback);
    default:
        UNREACHABLE();
    }
}

bool SDMCImporter::ImportTitle(const ContentSpecifier& specifier,
                               const ProgressCallback& callback) {
    decryptor->Reset(specifier.maximum_size);
    const FileUtil::DirectoryEntryCallable DirectoryEntryCallback =
        [this, size = config.sdmc_path.size(), callback,
         &DirectoryEntryCallback](u64* /*num_entries_out*/, const std::string& directory,
                                  const std::string& virtual_name) {
            if (FileUtil::IsDirectory(directory + virtual_name + "/")) {
                if (virtual_name == "cmd") {
                    return true; // Skip cmd (not used in Citra)
                }
                // Recursive call (necessary for DLCs)
                return FileUtil::ForeachDirectoryEntry(nullptr, directory + virtual_name + "/",
                                                       DirectoryEntryCallback);
            }
            const auto filepath = (directory + virtual_name).substr(size - 1);
            return decryptor->DecryptAndWriteFile(
                filepath,
                FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                    "Nintendo "
                    "3DS/00000000000000000000000000000000/00000000000000000000000000000000" +
                    filepath,
                callback);
        };
    const auto path = fmt::format("title/{:08x}/{:08x}/content/", (specifier.id >> 32),
                                  (specifier.id & 0xFFFFFFFF));
    return FileUtil::ForeachDirectoryEntry(nullptr, config.sdmc_path + path,
                                           DirectoryEntryCallback);
}

bool SDMCImporter::ImportSavegame(u64 id, [[maybe_unused]] const ProgressCallback& callback) {
    const auto path = fmt::format("title/{:08x}/{:08x}/data/", (id >> 32), (id & 0xFFFFFFFF));

    DataContainer container(decryptor->DecryptFile(fmt::format("/{}00000001.sav", path)));
    if (!container.IsGood()) {
        return false;
    }

    SDSavegame save(std::move(container.GetIVFCLevel4Data()));
    if (!save.IsGood()) {
        return false;
    }

    return save.Extract(
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
        "Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/" + path);
}

bool SDMCImporter::ImportExtdata(u64 id, [[maybe_unused]] const ProgressCallback& callback) {
    const auto path = fmt::format("extdata/{:08x}/{:08x}/", (id >> 32), (id & 0xFFFFFFFF));
    SDExtdata extdata("/" + path, *decryptor);
    if (!extdata.IsGood()) {
        return false;
    }

    return extdata.Extract(
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
        "Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/" + path);
}

bool SDMCImporter::ImportSystemArchive(u64 id, [[maybe_unused]] const ProgressCallback& callback) {
    const auto path = fmt::format("{}{:08x}/{:08x}.app", config.system_archives_path, (id >> 32),
                                  (id & 0xFFFFFFFF));
    FileUtil::IOFile file(path, "rb");
    if (!file) {
        LOG_ERROR(Core, "Could not open {}", path);
        return false;
    }

    std::vector<u8> data(file.GetSize());
    if (file.ReadBytes(data.data(), data.size()) != data.size()) {
        LOG_ERROR(Core, "Failed to read from {}", path);
        return false;
    }

    const auto& romfs = LoadSharedRomFS(data);

    const auto target_path = fmt::format(
        "{}00000000000000000000000000000000/title/{:08x}/{:08x}/content/00000000.app.romfs",
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF));
    if (!FileUtil::CreateFullPath(target_path)) {
        LOG_ERROR(Core, "Could not create path {}", target_path);
        return false;
    }

    FileUtil::IOFile target(target_path, "wb");
    if (!target) {
        LOG_ERROR(Core, "Could not open {}", target_path);
        return false;
    }

    if (target.WriteBytes(romfs.data(), romfs.size()) != romfs.size()) {
        LOG_ERROR(Core, "Failed to write to {}", target_path);
        return false;
    }

    return true;
}

bool SDMCImporter::ImportSysdata(u64 id, [[maybe_unused]] const ProgressCallback& callback) {
    switch (id) {
    case 0: { // boot9.bin
        const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + BOOTROM9;
        LOG_INFO(Core, "Copying {} from {} to {}", BOOTROM9, config.bootrom_path, target_path);
        if (!FileUtil::CreateFullPath(target_path)) {
            return false;
        }
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
            nullptr, real_path,
            [is_new_3ds](u64* /*num_entries_out*/, const std::string& directory,
                         const std::string& virtual_name) {
                if (FileUtil::IsDirectory(directory + virtual_name)) {
                    return true;
                }

                const auto target_path =
                    fmt::format("{}00000000000000000000000000000000/title/00040138/{}/content/{}",
                                FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                                (is_new_3ds ? "20000003" : "00000003"), virtual_name);

                if (!FileUtil::CreateFullPath(target_path)) {
                    return false;
                }

                return FileUtil::Copy(directory + virtual_name, target_path);
            });
    }
    case 2: { // seed db
        const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SEED_DB;
        LOG_INFO(Core, "Dumping SeedDB from {} to {}", SEED_DB, config.seed_db_path, target_path);

        SeedDB target;
        if (!target.Load(target_path)) {
            LOG_ERROR(Core, "Could not load seeddb from {}", target_path);
            return false;
        }

        SeedDB source;
        if (!source.Load(config.seed_db_path)) {
            LOG_ERROR(Core, "Could not load seeddb from {}", config.seed_db_path);
            return false;
        }

        for (const auto& seed : source) {
            if (!target.Get(seed.title_id)) {
                LOG_INFO(Core, "Adding seed for {:16X}", seed.title_id);
                target.Add(seed);
            }
        }
        return target.Save(target_path);
    }
    case 3: { // secret sector
        const auto target_path =
            FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SECRET_SECTOR;
        LOG_INFO(Core, "Copying {} from {} to {}", SECRET_SECTOR, config.secret_sector_path,
                 target_path);
        if (!FileUtil::CreateFullPath(target_path)) {
            return false;
        }
        return FileUtil::Copy(config.secret_sector_path, target_path);
    }
    case 4: { // aes_keys.txt
        const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + AES_KEYS;
        if (!FileUtil::CreateFullPath(target_path)) {
            return false;
        }
        FileUtil::IOFile file(target_path, "w");
        if (!file) {
            return false;
        }
        file.WriteString("slot0x25KeyX=" + Key::KeyToString(Key::GetKeyX(0x25)) + "\n");
        return true;
    }
    case 5: { // Config savegame
        FileUtil::IOFile file(config.config_savegame_path, "rb");
        if (!file) {
            return false;
        }

        std::vector<u8> data(file.GetSize());
        if (file.ReadBytes(data.data(), data.size()) != data.size()) {
            return false;
        }

        DataContainer container(data);
        if (!container.IsGood()) {
            return false;
        }

        SDSavegame save(std::move(container.GetIVFCLevel4Data()));
        if (!save.IsGood()) {
            return false;
        }

        const auto target_path =
            fmt::format("{}data/00000000000000000000000000000000/sysdata/00010017/00000000/",
                        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
        if (!FileUtil::CreateFullPath(target_path)) {
            return false;
        }
        return save.ExtractDirectory(target_path, 1); // 1 = root
    }
    default:
        UNREACHABLE_MSG("Unexpected sysdata id {}", id);
    }
}

std::vector<ContentSpecifier> SDMCImporter::ListContent() const {
    std::vector<ContentSpecifier> content_list;
    ListTitle(content_list);
    ListExtdata(content_list);
    ListSystemArchive(content_list);
    ListSysdata(content_list);
    return content_list;
}

// Regex for half Title IDs
static const std::regex title_regex{"[0-9a-f]{8}"};

std::tuple<std::string, u64, EncryptionType, bool, std::vector<u16>> SDMCImporter::LoadTitleData(
    const std::string& path) const {
    // Remove trailing '/'
    const auto sdmc_path = config.sdmc_path.substr(0, config.sdmc_path.size() - 1);

    std::string title_metadata;
    const bool ret = FileUtil::ForeachDirectoryEntry(
        nullptr, sdmc_path + path,
        [&title_metadata](u64* /*num_entries_out*/, const std::string& directory,
                          const std::string& virtual_name) {
            if (FileUtil::IsDirectory(directory + virtual_name)) {
                return true;
            }

            if (virtual_name.substr(virtual_name.size() - 3) == "tmd" &&
                std::regex_match(virtual_name.substr(0, 8), title_regex)) {

                title_metadata = virtual_name;
                return false;
            }

            return true;
        });

    if (ret) { // TMD not found
        return {};
    }

    if (!FileUtil::Exists(sdmc_path + path + title_metadata)) {
        // Probably TMD is not directly inside, aborting.
        return {};
    }

    TitleMetadata tmd;
    tmd.Load(decryptor->DecryptFile(path + title_metadata));

    const auto boot_content_path = fmt::format("{}{:08x}.app", path, tmd.GetBootContentID());

    NCCHContainer ncch(config.sdmc_path, boot_content_path);
    auto ret2 = ncch.Load();
    if (ret2 != ResultStatus::Success) {
        LOG_CRITICAL(Core, "failed to load ncch: {}", ret2);
        return {};
    }

    std::vector<u8> smdh_buffer;
    if (ncch.LoadSectionExeFS("icon", smdh_buffer) != ResultStatus::Success) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS");
        return {};
    }

    if (smdh_buffer.size() != sizeof(SMDH)) {
        LOG_ERROR(Core, "ExeFS icon section size is not correct");
        return {};
    }

    SMDH smdh;
    std::memcpy(&smdh, smdh_buffer.data(), smdh_buffer.size());

    u64 extdata_id{};
    ncch.ReadExtdataId(extdata_id);

    EncryptionType encryption = EncryptionType::None;
    ncch.ReadEncryptionType(encryption);

    bool seed_crypto{};
    ncch.ReadSeedCrypto(seed_crypto);
    return {Common::UTF16BufferToUTF8(smdh.GetShortTitle(SMDH::TitleLanguage::English)), extdata_id,
            encryption, seed_crypto, smdh.GetIcon(false)};
}

void SDMCImporter::ListTitle(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [this, &out, &sdmc_path = config.sdmc_path](ContentType type,
                                                                              u64 high_id) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, fmt::format("{}title/{:08x}/", sdmc_path, high_id),
            [this, type, high_id, &out](u64* /*num_entries_out*/, const std::string& directory,
                                        const std::string& virtual_name) {
                if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                    return true;
                }

                if (!std::regex_match(virtual_name, title_regex)) {
                    return true;
                }

                const u64 id = (high_id << 32) + std::stoull(virtual_name, nullptr, 16);
                const auto citra_path = fmt::format(
                    "{}Nintendo "
                    "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/"
                    "{:08x}/{}/",
                    FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), high_id, virtual_name);

                if (FileUtil::Exists(directory + virtual_name + "/content/")) {
                    const auto content_path =
                        fmt::format("/title/{:08x}/{}/content/", high_id, virtual_name);
                    const auto& [name, extdata_id, encryption, seed_crypto, icon] =
                        LoadTitleData(content_path);
                    out.push_back(
                        {type, id, FileUtil::Exists(citra_path + "content/"),
                         FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/content/"),
                         name, extdata_id, encryption, seed_crypto, icon});
                }

                if (type != ContentType::Application) {
                    return true;
                }
                if (FileUtil::Exists(directory + virtual_name + "/data/")) {
                    // Savegames can be uninitialized.
                    // TODO: Is there a better way of checking this other than performing the
                    // decryption? (Very costy)
                    DataContainer container(decryptor->DecryptFile(
                        fmt::format("/title/{:08x}/{}/data/00000001.sav", high_id, virtual_name)));
                    if (!container.IsGood()) {
                        return true;
                    }

                    out.push_back(
                        {ContentType::Savegame, id, FileUtil::Exists(citra_path + "data/"),
                         FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/data/")});
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
            if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                return true;
            }

            if (!std::regex_match(virtual_name, title_regex)) {
                return true;
            }

            const u64 id = std::stoull(virtual_name, nullptr, 16);
            const auto citra_path =
                fmt::format("{}Nintendo "
                            "3DS/00000000000000000000000000000000/00000000000000000000000000000000/"
                            "extdata/00000000/{}",
                            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), virtual_name);
            out.push_back({ContentType::Extdata, id, FileUtil::Exists(citra_path),
                           FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/")});
            return true;
        });
}

void SDMCImporter::ListSystemArchive(std::vector<ContentSpecifier>& out) const {
    constexpr std::array<std::pair<u64, const char*>, 8> SystemArchives{{
        {0x0004009b'00010202, "Mii Data"},
        {0x0004009b'00010402, "Region Manifest"},
        {0x0004009b'00014002, "Shared Font (JPN/EUR/USA)"},
        {0x0004009b'00014102, "Shared Font (CHN)"},
        {0x0004009b'00014202, "Shared Font (KOR)"},
        {0x0004009b'00014302, "Shared Font (TWN)"},
        {0x000400db'00010302, "Bad word list"},
    }};

    for (const auto& [id, name] : SystemArchives) {
        const auto path = fmt::format("{}{:08x}/{:08x}.app", config.system_archives_path,
                                      (id >> 32), (id & 0xFFFFFFFF));
        if (FileUtil::Exists(path)) {
            const auto target_path = fmt::format(
                "{}00000000000000000000000000000000/title/{:08x}/{:08x}/content/",
                FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF));
            out.push_back({ContentType::SystemArchive, id, FileUtil::Exists(target_path),
                           FileUtil::GetSize(path), name});
        }
    }
}

void SDMCImporter::ListSysdata(std::vector<ContentSpecifier>& out) const {
#define CHECK_CONTENT(id, var_path, citra_path, display_name)                                      \
    if (!var_path.empty()) {                                                                       \
        out.push_back({ContentType::Sysdata, id, FileUtil::Exists(citra_path),                     \
                       FileUtil::GetSize(var_path), display_name});                                \
    }

    {
        const auto sysdata_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir);
        CHECK_CONTENT(0, config.bootrom_path, sysdata_path + BOOTROM9, BOOTROM9);
        CHECK_CONTENT(3, config.secret_sector_path, sysdata_path + SECRET_SECTOR, SECRET_SECTOR);
        if (!config.bootrom_path.empty()) {
            // 47 bytes = "slot0x26KeyX=<32>\r\n" is only for Windows,
            // but it's maximum_size so probably okay
            out.push_back(
                {ContentType::Sysdata, 4, FileUtil::Exists(sysdata_path + AES_KEYS), 47, AES_KEYS});
        }
        CHECK_CONTENT(5, config.config_savegame_path,
                      fmt::format("{}data/00000000000000000000000000000000/sysdata/00010017/",
                                  FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)),
                      "Config savegame");
    }

#undef CHECK_CONTENT

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

        const auto citra_path = fmt::format(
            "{}00000000000000000000000000000000/title/00040138/{}/content/",
            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (is_new ? "20000003" : "00000003"));
        if (!config.safe_mode_firm_path.empty()) {
            out.push_back({ContentType::Sysdata, 1, FileUtil::Exists(citra_path),
                           FileUtil::GetDirectoryTreeSize(config.safe_mode_firm_path),
                           "Safe mode firm"});
        }
    } while (0);

    // Check for seeddb
    if (config.seed_db_path.empty()) {
        return;
    }

    const auto target_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SEED_DB;
    SeedDB target;
    if (!target.Load(target_path)) {
        LOG_ERROR(Core, "Could not load seeddb from {}", target_path);
        return;
    }

    SeedDB source;
    if (!source.Load(config.seed_db_path)) {
        LOG_ERROR(Core, "Could not load seeddb from {}", config.seed_db_path);
        return;
    }

    bool exists = true; // Whether the DB already 'exists', i.e. no new seeds can be found
    for (const auto& seed : source) {
        if (!target.Get(seed.title_id)) {
            exists = false;
            break;
        }
    }
    out.push_back(
        {ContentType::Sysdata, 2, exists, FileUtil::GetSize(config.seed_db_path), SEED_DB});
}

void SDMCImporter::DeleteContent(const ContentSpecifier& specifier) {
    switch (specifier.type) {
    case ContentType::Application:
    case ContentType::Update:
    case ContentType::DLC:
        return DeleteTitle(specifier.id);
    case ContentType::Savegame:
        return DeleteSavegame(specifier.id);
    case ContentType::Extdata:
        return DeleteExtdata(specifier.id);
    case ContentType::SystemArchive:
        return DeleteSystemArchive(specifier.id);
    case ContentType::Sysdata:
        return DeleteSysdata(specifier.id);
    default:
        UNREACHABLE();
    }
}

void SDMCImporter::DeleteTitle(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}Nintendo "
        "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/{:08x}/{:08x}/"
        "content/",
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteSavegame(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}Nintendo "
        "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/{:08x}/{:08x}/"
        "data/",
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteExtdata(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}Nintendo "
        "3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/{:08x}/"
        "{:08x}/",
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteSystemArchive(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}00000000000000000000000000000000/title/{:08x}/{:08x}/content/",
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteSysdata(u64 id) const {
    switch (id) {
    case 0: { // boot9.bin
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + BOOTROM9);
    }
    case 1: { // safe mode firm
        const bool is_new_3ds = FileUtil::Exists(config.safe_mode_firm_path + "new/");
        const auto target_path =
            fmt::format("{}00000000000000000000000000000000/title/00040138/{}/",
                        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                        (is_new_3ds ? "20000003" : "00000003"));
        FileUtil::DeleteDirRecursively(target_path);
    }
    case 2: { // seed db
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SEED_DB);
    }
    case 3: { // secret sector
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SECRET_SECTOR);
    }
    case 4: { // aes_keys.txt
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + AES_KEYS);
    }
    case 5: { // Config savegame
        FileUtil::DeleteDirRecursively(
            fmt::format("{}data/00000000000000000000000000000000/sysdata/00010017/",
                        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)));
    }
    default:
        UNREACHABLE_MSG("Unexpected sysdata id {}", id);
    }
}

std::vector<Config> LoadPresetConfig(std::string mount_point) {
    if (mount_point.back() != '/' && mount_point.back() != '\\') {
        mount_point += '/';
    }

    // Not a Nintendo 3DS sd card at all
    if (!FileUtil::Exists(mount_point + "Nintendo 3DS/")) {
        return {};
    }

    Config config_template{};
    config_template.user_path = FileUtil::GetUserPath(FileUtil::UserPath::UserDir);

    // Load dumped data paths if using our dumper
    if (FileUtil::Exists(mount_point + "threeSD/")) {
#define LOAD_DATA(var, path)                                                                       \
    if (FileUtil::Exists(mount_point + "threeSD/" + path)) {                                       \
        config_template.var = mount_point + "threeSD/" + path;                                     \
    }

        LOAD_DATA(movable_sed_path, MOVABLE_SED);
        LOAD_DATA(bootrom_path, BOOTROM9);
        LOAD_DATA(safe_mode_firm_path, "firm/");
        LOAD_DATA(seed_db_path, SEED_DB);
        LOAD_DATA(secret_sector_path, SECRET_SECTOR);
        LOAD_DATA(config_savegame_path, "config.sav");
        LOAD_DATA(system_archives_path, "sysarchives/");
#undef LOAD_DATA
    }

    // Regex for 3DS ID0 and ID1
    const std::regex id_regex{"[0-9a-f]{32}"};

    // Load SDMC dir
    std::vector<Config> out;
    const auto ProcessDirectory = [&id_regex, &config_template, &out](const std::string& path) {
        return FileUtil::ForeachDirectoryEntry(
            nullptr, path,
            [&id_regex, &config_template, &out](u64* /*num_entries_out*/,
                                                const std::string& directory,
                                                const std::string& virtual_name) {
                if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                    return true;
                }

                if (!std::regex_match(virtual_name, id_regex)) {
                    return true;
                }

                Config config = config_template;
                config.sdmc_path = directory + virtual_name + "/";
                out.push_back(config);
                return true;
            });
    };

    FileUtil::ForeachDirectoryEntry(
        nullptr, mount_point + "Nintendo 3DS/",
        [&id_regex, &ProcessDirectory](u64* /*num_entries_out*/, const std::string& directory,
                                       const std::string& virtual_name) {
            if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                return true;
            }

            if (!std::regex_match(virtual_name, id_regex)) {
                return true;
            }

            return ProcessDirectory(directory + virtual_name + "/");
        });

    return out;
}

} // namespace Core
