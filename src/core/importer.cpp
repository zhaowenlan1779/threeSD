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
#include "core/extdata.h"
#include "core/importer.h"
#include "core/key/key.h"
#include "core/ncch/certificate.h"
#include "core/ncch/cia_builder.h"
#include "core/ncch/ncch_container.h"
#include "core/ncch/seed_db.h"
#include "core/ncch/smdh.h"
#include "core/ncch/title_metadata.h"
#include "core/savegame.h"
#include "core/title_db.h"

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

    if (!config.seed_db_path.empty()) {
        Seeds::Load(config.seed_db_path);
    }
    if (!config.certs_db_path.empty()) {
        Certs::Load(config.certs_db_path);
    }

    decryptor = std::make_unique<SDMCDecryptor>(config.sdmc_path);
    cia_builder = std::make_unique<CIABuilder>();

    // Load SDMC Title DB
    {
        DataContainer container(decryptor->DecryptFile("/dbs/title.db"));
        std::vector<std::vector<u8>> data;
        if (container.IsGood() && container.GetIVFCLevel4Data(data)) {
            sdmc_title_db = std::make_unique<TitleDB>(std::move(data[0]));
        }
    }
    if (!sdmc_title_db || !sdmc_title_db->IsGood()) {
        LOG_WARNING(Core, "SDMC title.db invalid");
        sdmc_title_db.reset();
    }

    // Load NAND Title DB
    if (!config.nand_title_db_path.empty()) {
        nand_title_db = std::make_unique<TitleDB>(config.nand_title_db_path);
    }
    if (!nand_title_db || !nand_title_db->IsGood()) {
        LOG_WARNING(Core, "NAND title.db invalid");
        nand_title_db.reset();
    }

    FileUtil::SetUserPath(config.user_path);
    return true;
}

bool SDMCImporter::IsGood() const {
    return is_good;
}

void SDMCImporter::AbortImporting() {
    decryptor->Abort();
}

bool SDMCImporter::ImportContent(const ContentSpecifier& specifier,
                                 const Common::ProgressCallback& callback) {
    if (!ImportContentImpl(specifier, callback)) {
        DeleteContent(specifier);
        return false;
    }
    return true;
}

bool SDMCImporter::ImportContentImpl(const ContentSpecifier& specifier,
                                     const Common::ProgressCallback& callback) {
    switch (specifier.type) {
    case ContentType::Application:
    case ContentType::Update:
    case ContentType::DLC:
        return ImportTitle(specifier, callback);
    case ContentType::Savegame:
        if ((specifier.id >> 32) == 0) {
            return ImportNandSavegame(specifier.id, callback);
        } else {
            return ImportSavegame(specifier.id, callback);
        }
    case ContentType::Extdata:
        if ((specifier.id >> 32) == 0) {
            return ImportExtdata(specifier.id, callback);
        } else {
            return ImportNandExtdata(specifier.id, callback);
        }
    case ContentType::SystemArchive:
        return ImportSystemArchive(specifier.id, callback);
    case ContentType::Sysdata:
        return ImportSysdata(specifier.id, callback);
    case ContentType::SystemTitle:
    case ContentType::SystemApplet:
        return ImportNandTitle(specifier, callback);
    default:
        UNREACHABLE();
    }
}

namespace {

template <typename Dec>
bool ImportTitleGeneric(Dec& decryptor, const std::string& base_path,
                        const ContentSpecifier& specifier,
                        const std::function<bool(const std::string&)>& decryption_func) {

    decryptor.Reset(specifier.maximum_size);
    const FileUtil::DirectoryEntryCallable DirectoryEntryCallback =
        [size = base_path.size(), &DirectoryEntryCallback,
         &decryption_func](u64* /*num_entries_out*/, const std::string& directory,
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
            return decryption_func(filepath);
        };
    const auto path = fmt::format("title/{:08x}/{:08x}/content/", (specifier.id >> 32),
                                  (specifier.id & 0xFFFFFFFF));
    return FileUtil::ForeachDirectoryEntry(nullptr, base_path + path, DirectoryEntryCallback);
}

} // namespace

bool SDMCImporter::ImportTitle(const ContentSpecifier& specifier,
                               const Common::ProgressCallback& callback) {
    return ImportTitleGeneric(
        *decryptor, config.sdmc_path, specifier, [this, &callback](const std::string& filepath) {
            return decryptor->DecryptAndWriteFile(
                filepath,
                FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                    "Nintendo "
                    "3DS/00000000000000000000000000000000/00000000000000000000000000000000" +
                    filepath,
                callback);
        });
}

bool SDMCImporter::ImportNandTitle(const ContentSpecifier& specifier,
                                   const Common::ProgressCallback& callback) {

    const auto base_path =
        config.system_titles_path.substr(0, config.system_titles_path.size() - 6);
    QuickDecryptor quick_decryptor;
    return ImportTitleGeneric(
        quick_decryptor, base_path, specifier,
        [&base_path, &quick_decryptor, &callback](const std::string& filepath) {
            const auto physical_path = base_path + filepath.substr(1);
            const auto citra_path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                    "00000000000000000000000000000000" + filepath;
            if (!FileUtil::CreateFullPath(citra_path)) {
                LOG_ERROR(Core, "Could not create path {}", citra_path);
                return false;
            }
            // Crypto is not set: plain copy with progress.
            return quick_decryptor.CryptAndWriteFile(
                std::make_shared<FileUtil::IOFile>(physical_path, "rb"),
                FileUtil::GetSize(physical_path),
                std::make_shared<FileUtil::IOFile>(citra_path, "wb"), callback);
        });
}

bool SDMCImporter::ImportSavegame(u64 id,
                                  [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("title/{:08x}/{:08x}/data/", (id >> 32), (id & 0xFFFFFFFF));

    DataContainer container(decryptor->DecryptFile(fmt::format("/{}00000001.sav", path)));
    if (!container.IsGood()) {
        return false;
    }

    std::vector<std::vector<u8>> container_data;
    if (!container.GetIVFCLevel4Data(container_data)) {
        return false;
    }

    Savegame save(std::move(container_data));
    if (!save.IsGood()) {
        return false;
    }

    return save.Extract(
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
        "Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/" + path);
}

bool SDMCImporter::ImportNandSavegame(u64 id,
                                      [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("sysdata/{:08x}/00000000", (id & 0xFFFFFFFF));

    FileUtil::IOFile file(config.nand_data_path + path, "rb");
    std::vector<u8> data = file.GetData();
    if (data.empty()) {
        LOG_ERROR(Core, "Failed to read from {}", path);
        return false;
    }

    DataContainer container(std::move(data));
    std::vector<std::vector<u8>> container_data;
    if (!container.GetIVFCLevel4Data(container_data)) {
        return false;
    }

    Savegame save(std::move(container_data));
    if (!save.IsGood()) {
        return false;
    }

    return save.ExtractDirectory(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                     "data/00000000000000000000000000000000/" + path + "/",
                                 1);
}

bool SDMCImporter::ImportExtdata(u64 id,
                                 [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("extdata/{:08x}/{:08x}/", (id >> 32), (id & 0xFFFFFFFF));
    Extdata extdata("/" + path, *decryptor);
    if (!extdata.IsGood()) {
        return false;
    }

    return extdata.Extract(
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
        "Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/" + path);
}

bool SDMCImporter::ImportNandExtdata(u64 id,
                                     [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("extdata/{:08x}/{:08x}/", (id >> 32), (id & 0xFFFFFFFF));
    Extdata extdata(config.nand_data_path + path);
    if (!extdata.IsGood()) {
        return false;
    }

    return extdata.Extract(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                           "data/00000000000000000000000000000000/" + path);
}

bool SDMCImporter::ImportSystemArchive(u64 id,
                                       [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("{}{:08x}/{:08x}.app", config.system_archives_path, (id >> 32),
                                  (id & 0xFFFFFFFF));
    FileUtil::IOFile file(path, "rb");
    std::vector<u8> data = file.GetData();
    if (data.empty()) {
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

    return FileUtil::WriteBytesToFile(target_path, romfs.data(), romfs.size());
}

bool SDMCImporter::ImportSysdata(u64 id,
                                 [[maybe_unused]] const Common::ProgressCallback& callback) {
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
        file.WriteString("slot0x18KeyX=" + Key::KeyToString(Key::GetKeyX(0x18)) + "\n");
        file.WriteString("slot0x1BKeyX=" + Key::KeyToString(Key::GetKeyX(0x1B)) + "\n");
        return true;
    }
    case 5: { // Config savegame
        FileUtil::IOFile file(config.config_savegame_path, "rb");
        std::vector<u8> data = file.GetData();
        if (data.empty()) {
            return false;
        }

        DataContainer container(data);
        if (!container.IsGood()) {
            return false;
        }

        std::vector<std::vector<u8>> container_data;
        if (!container.GetIVFCLevel4Data(container_data)) {
            return false;
        }

        Savegame save(std::move(container_data));
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
    ListNandTitle(content_list);
    ListNandSavegame(content_list);
    ListExtdata(content_list);
    ListSystemArchive(content_list);
    ListSysdata(content_list);
    return content_list;
}

// Regex for half Title IDs
static const std::regex title_regex{"[0-9a-f]{8}"};

static std::string FindTMD(const std::string& path) {
    std::string title_metadata;
    const bool ret = FileUtil::ForeachDirectoryEntry(
        nullptr, path,
        [&title_metadata](u64* /*num_entries_out*/, const std::string& directory,
                          const std::string& virtual_name) {
            if (FileUtil::IsDirectory(directory + virtual_name)) {
                return true;
            }

            if (virtual_name.size() == 12 &&
                virtual_name.substr(virtual_name.size() - 4) == ".tmd" &&
                std::regex_match(virtual_name.substr(0, 8), title_regex)) {

                // We would like to find the TMD with the smallest content ID,
                // as that would be the finalized version, not the version
                // pending installation
                title_metadata =
                    title_metadata.empty() ? virtual_name : std::min(title_metadata, virtual_name);
                return true;
            }

            return true;
        });

    if (title_metadata.empty()) { // TMD not found
        return {};
    }

    if (!FileUtil::Exists(path + title_metadata)) {
        // Probably TMD is not directly inside, aborting.
        return {};
    }
    return path + title_metadata;
}

bool SDMCImporter::LoadTMD(ContentType type, u64 id, TitleMetadata& out) const {
    const bool is_nand = type == ContentType::SystemTitle;

    auto& title_db = is_nand ? nand_title_db : sdmc_title_db;
    const auto physical_path =
        is_nand ? fmt::format("{}{:08x}/{:08x}/content/", config.system_titles_path, (id >> 32),
                              (id & 0xFFFFFFFF))
                : fmt::format("{}title/{:08x}/{:08x}/content/", config.sdmc_path, (id >> 32),
                              (id & 0xFFFFFFFF));

    std::string tmd_path;
    if (title_db && title_db->titles.count(id)) {
        tmd_path =
            fmt::format("{}{:08x}.tmd", physical_path, title_db->titles.at(id).tmd_content_id);
    } else {
        LOG_WARNING(Core, "Title {:016x} does not exist in title.db", id);
        tmd_path = FindTMD(physical_path);
        if (tmd_path.empty()) {
            return false;
        }
    }

    if (is_nand) {
        FileUtil::IOFile file(tmd_path, "rb");
        if (!file || file.GetSize() > 1024 * 1024) {
            LOG_ERROR(Core, "Could not open {} or file too big", tmd_path);
            return false;
        }
        return out.Load(file.GetData()) == ResultStatus::Success;
    } else {
        return out.Load(decryptor->DecryptFile(tmd_path.substr(config.sdmc_path.size() - 1))) ==
               ResultStatus::Success;
    }
}

// English short title name, extdata id, encryption, seed, icon
using TitleData = std::tuple<std::string, u64, EncryptionType, bool, std::vector<u16>>;

TitleData LoadTitleData(NCCHContainer& ncch) {
    std::string codeset_name;
    ncch.ReadCodesetName(codeset_name);

    u64 program_id{};
    ncch.ReadProgramId(program_id);

    std::string title_name_from_codeset;
    if (!codeset_name.empty()) {
        title_name_from_codeset =
            fmt::format("{} (0x{:016x})", std::move(codeset_name), program_id);
    }

    std::vector<u8> smdh_buffer;
    if (ncch.LoadSectionExeFS("icon", smdh_buffer) != ResultStatus::Success) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS");
        TitleData data{};
        std::get<0>(data) = std::move(title_name_from_codeset);
        return data;
    }

    if (smdh_buffer.size() != sizeof(SMDH)) {
        LOG_ERROR(Core, "ExeFS icon section size is not correct");
        TitleData data{};
        std::get<0>(data) = std::move(title_name_from_codeset);
        return data;
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

static std::string NormalizeFilename(std::string filename) {
    static constexpr std::array<char, 8> IllegalCharacters{
        {':', '/', '\\', '"', '*', '?', '\n', '\r'}};

    const auto pred = [](char c) {
        return std::find(IllegalCharacters.begin(), IllegalCharacters.end(), c) !=
               IllegalCharacters.end();
    };
    std::replace_if(filename.begin(), filename.end(), pred, ' ');

    std::string result;
    for (std::size_t i = 0; i < filename.size(); ++i) {
        if (i < filename.size() - 1 && filename[i] == ' ' && filename[i + 1] == ' ') {
            continue;
        }
        result.push_back(filename[i]);
    }
    return result;
}

static std::string GetTitleFileName(NCCHContainer& ncch) {
    std::string codeset_name;
    ncch.ReadCodesetName(codeset_name);

    std::string product_code;
    ncch.ReadProductCode(product_code);

    u64 program_id{};
    ncch.ReadProgramId(program_id);

    std::vector<u8> smdh_buffer;
    if (ncch.LoadSectionExeFS("icon", smdh_buffer) != ResultStatus::Success ||
        smdh_buffer.size() != sizeof(SMDH)) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS or size incorrect");
        return NormalizeFilename(
            fmt::format("{:016x} {} ({})", program_id, codeset_name, product_code));
    } else {
        SMDH smdh;
        std::memcpy(&smdh, smdh_buffer.data(), smdh_buffer.size());
        const auto short_title =
            Common::UTF16BufferToUTF8(smdh.GetShortTitle(SMDH::TitleLanguage::English));
        return NormalizeFilename(fmt::format("{:016x} {} ({}) ({})", program_id, short_title,
                                             product_code, smdh.GetRegionString()));
    }
}

bool SDMCImporter::DumpCXI(const ContentSpecifier& specifier, std::string destination,
                           const Common::ProgressCallback& callback, bool auto_filename) {

    if (specifier.type != ContentType::Application) {
        LOG_ERROR(Core, "Unsupported specifier type {}", static_cast<int>(specifier.type));
        return false;
    }

    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }

    const auto boot_content_path =
        fmt::format("/title/{:08x}/{:08x}/content/{:08x}.app", specifier.id >> 32,
                    (specifier.id & 0xFFFFFFFF), tmd.GetBootContentID());
    dump_cxi_ncch = std::make_unique<NCCHContainer>(
        std::make_shared<SDMCFile>(config.sdmc_path, boot_content_path, "rb"));

    if (auto_filename) {
        if (destination.back() != '/' && destination.back() != '\\') {
            destination.push_back('/');
        }
        destination.append(GetTitleFileName(*dump_cxi_ncch)).append(".cxi");
    }

    if (!FileUtil::CreateFullPath(destination)) {
        LOG_ERROR(Core, "Failed to create path {}", destination);
        return false;
    }

    if (dump_cxi_ncch->DecryptToFile(std::make_shared<FileUtil::IOFile>(destination, "wb"),
                                     callback) == ResultStatus::Success) {
        return true;
    }

    FileUtil::Delete(destination);
    return false;
}

void SDMCImporter::AbortDumpCXI() {
    dump_cxi_ncch->AbortDecryptToFile();
}

bool SDMCImporter::BuildCIA(CIABuildType type, const ContentSpecifier& specifier,
                            std::string destination, const Common::ProgressCallback& callback,
                            bool auto_filename) {

    if (config.certs_db_path.empty()) {
        LOG_ERROR(Core, "Missing certs.db");
        return false;
    }

    if (specifier.type != ContentType::Application && specifier.type != ContentType::Update &&
        specifier.type != ContentType::DLC && specifier.type != ContentType::SystemTitle) {

        LOG_ERROR(Core, "Unsupported specifier type {}", static_cast<int>(specifier.type));
        return false;
    }

    // Load TMD
    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }

    const bool is_nand = specifier.type == ContentType::SystemTitle;
    const auto physical_path =
        is_nand ? fmt::format("{}{:08x}/{:08x}/content/", config.system_titles_path,
                              (specifier.id >> 32), (specifier.id & 0xFFFFFFFF))
                : fmt::format("{}title/{:08x}/{:08x}/content/", config.sdmc_path,
                              (specifier.id >> 32), (specifier.id & 0xFFFFFFFF));

    if (auto_filename) {
        if (destination.back() != '/' && destination.back() != '\\') {
            destination.push_back('/');
        }
        const auto boot_content_path =
            fmt::format("{}{:08x}.app", physical_path, tmd.GetBootContentID());
        if (is_nand) {
            NCCHContainer ncch(std::make_shared<FileUtil::IOFile>(boot_content_path, "rb"));
            destination.append(GetTitleFileName(ncch)).append(".cia");
        } else {
            const auto relative_path = boot_content_path.substr(config.sdmc_path.size() - 1);
            NCCHContainer ncch(std::make_shared<SDMCFile>(config.sdmc_path, relative_path, "rb"));
            destination.append(GetTitleFileName(ncch)).append(".cia");
        }
    }

    const bool ret = cia_builder->Init(type, destination, tmd, config,
                                       FileUtil::GetDirectoryTreeSize(physical_path), callback);
    if (!ret) {
        FileUtil::Delete(destination);
        return false;
    }

    const FileUtil::DirectoryEntryCallable DirectoryEntryCallback =
        [this, tmd, is_nand, specifier, &DirectoryEntryCallback](u64* /*num_entries_out*/,
                                                                 const std::string& directory,
                                                                 const std::string& virtual_name) {
            if (FileUtil::IsDirectory(directory + virtual_name + "/")) {
                if (virtual_name == "cmd") {
                    return true; // Skip cmd (not used in Citra)
                }
                // Recursive call (necessary for DLCs)
                return FileUtil::ForeachDirectoryEntry(nullptr, directory + virtual_name + "/",
                                                       DirectoryEntryCallback);
            }

            static const std::regex app_regex{"([0-9a-f]{8})\\.app"};

            std::smatch match;
            if (!std::regex_match(virtual_name, match, app_regex)) {
                return true;
            }
            ASSERT(match.size() >= 2);

            const u32 id = static_cast<u32>(std::stoul(match[1], nullptr, 16));
            if (!tmd.HasContentID(id)) {
                LOG_WARNING(Core, "Ignoring content {} (not in TMD)", directory + virtual_name);
                return true;
            }

            if (is_nand) {
                NCCHContainer ncch(
                    std::make_shared<FileUtil::IOFile>(directory + virtual_name, "rb"));
                return cia_builder->AddContent(id, ncch);
            } else {
                const auto relative_path =
                    directory.substr(config.sdmc_path.size() - 1) + virtual_name;
                NCCHContainer ncch(
                    std::make_shared<SDMCFile>(config.sdmc_path, relative_path, "rb"));
                return cia_builder->AddContent(id, ncch);
            }
        };

    if (FileUtil::ForeachDirectoryEntry(nullptr, physical_path, DirectoryEntryCallback) &&
        cia_builder->Finalize()) {

        return true;
    }

    FileUtil::Delete(destination);
    return false;
}

void SDMCImporter::AbortBuildCIA() {
    cia_builder->Abort();
}

void SDMCImporter::ListTitle(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [this, &out, &sdmc_path = config.sdmc_path](ContentType type,
                                                                              u64 high_id) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, fmt::format("{}title/{:08x}/", sdmc_path, high_id),
            [this, &sdmc_path, type, high_id, &out](u64* /*num_entries_out*/,
                                                    const std::string& directory,
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
                    do {
                        TitleMetadata tmd;
                        if (!LoadTMD(type, id, tmd)) {
                            out.push_back({type, id, FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(directory + virtual_name +
                                                                          "/content/")});
                            break;
                        }

                        const auto boot_content_path =
                            fmt::format("/title/{:08x}/{}/content/{:08x}.app", high_id,
                                        virtual_name, tmd.GetBootContentID());
                        NCCHContainer ncch(
                            std::make_shared<SDMCFile>(sdmc_path, boot_content_path, "rb"));
                        if (ncch.Load() != ResultStatus::Success) {
                            LOG_WARNING(Core, "Could not load NCCH {}", boot_content_path);
                            out.push_back({type, id, FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(directory + virtual_name +
                                                                          "/content/")});
                            break;
                        }

                        const auto& [name, extdata_id, encryption, seed_crypto, icon] =
                            LoadTitleData(ncch);
                        out.push_back(
                            {type, id, FileUtil::Exists(citra_path + "content/"),
                             FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/content/"),
                             name, extdata_id, encryption, seed_crypto, icon});
                    } while (false);
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

// TODO: Simplify.
void SDMCImporter::ListNandTitle(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [this, &out,
                                   &system_titles_path = config.system_titles_path](u64 high_id) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, fmt::format("{}{:08x}/", system_titles_path, high_id),
            [this, high_id, &out](u64* /*num_entries_out*/, const std::string& directory,
                                  const std::string& virtual_name) {
                if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                    return true;
                }

                if (!std::regex_match(virtual_name, title_regex)) {
                    return true;
                }

                const u64 id = (high_id << 32) + std::stoull(virtual_name, nullptr, 16);
                const auto citra_path = fmt::format(
                    "{}00000000000000000000000000000000/title/{:08x}/{}/",
                    FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), high_id, virtual_name);

                const auto content_path = directory + virtual_name + "/content/";
                if (FileUtil::Exists(content_path)) {
                    do {
                        TitleMetadata tmd;
                        if (!LoadTMD(ContentType::SystemTitle, id, tmd)) {
                            out.push_back({ContentType::SystemTitle, id,
                                           FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(content_path)});
                            break;
                        }

                        const auto boot_content_path =
                            fmt::format("{}{:08x}.app", content_path, tmd.GetBootContentID());
                        NCCHContainer ncch(
                            std::make_shared<FileUtil::IOFile>(boot_content_path, "rb"));
                        if (ncch.Load() != ResultStatus::Success) {
                            LOG_WARNING(Core, "Could not load NCCH {}", boot_content_path);
                            out.push_back({ContentType::SystemTitle, id,
                                           FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(content_path)});
                            break;
                        }

                        const auto& [name, extdata_id, encryption, seed_crypto, icon] =
                            LoadTitleData(ncch);
                        const auto type = (id >> 32) == 0x00040030 ? ContentType::SystemApplet
                                                                   : ContentType::SystemTitle;
                        out.push_back(
                            {type, id, FileUtil::Exists(citra_path + "content/"),
                             FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/content/"),
                             name, extdata_id, encryption, seed_crypto, icon});
                    } while (false);
                }
                return true;
            });
    };

    ProcessDirectory(0x00040010);
    ProcessDirectory(0x0004001b);
    ProcessDirectory(0x00040030);
    ProcessDirectory(0x000400db);
    ProcessDirectory(0x00040130);
}

void SDMCImporter::ListNandSavegame(std::vector<ContentSpecifier>& out) const {
    FileUtil::ForeachDirectoryEntry(
        nullptr, fmt::format("{}sysdata/", config.nand_data_path),
        [&out](u64* /*num_entries_out*/, const std::string& directory,
               const std::string& virtual_name) {
            if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                return true;
            }

            if (!std::regex_match(virtual_name, title_regex)) {
                return true;
            }

            const auto path = directory + virtual_name + "/00000000";

            // Read the file to test.
            FileUtil::IOFile file(path, "rb");
            std::vector<u8> data = file.GetData();
            if (data.empty()) {
                LOG_ERROR(Core, "Could not read from {}", path);
                return false;
            }

            DataContainer container(std::move(data));
            if (!container.IsGood()) {
                return true;
            }

            const u64 id = std::stoull(virtual_name, nullptr, 16);
            const auto citra_path =
                fmt::format("{}data/00000000000000000000000000000000/sysdata/{}/00000000",
                            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), virtual_name);
            out.push_back(
                {ContentType::Savegame, id, FileUtil::Exists(citra_path), FileUtil::GetSize(path)});
            return true;
        });
}

void SDMCImporter::ListExtdata(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [&out](u64 id_high, const std::string& path,
                                         const std::string& citra_path_template) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, path,
            [&out, id_high, citra_path_template](u64* /*num_entries_out*/,
                                                 const std::string& directory,
                                                 const std::string& virtual_name) {
                if (!FileUtil::IsDirectory(directory + virtual_name + "/")) {
                    return true;
                }

                if (!std::regex_match(virtual_name, title_regex)) {
                    return true;
                }

                const u64 id = std::stoull(virtual_name, nullptr, 16);
                const auto citra_path = fmt::format(citra_path_template, virtual_name);
                out.push_back({ContentType::Extdata, (id_high << 32) | id,
                               FileUtil::Exists(citra_path),
                               FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/")});
                return true;
            });
    };
    ProcessDirectory(0, fmt::format("{}extdata/00000000/", config.sdmc_path),
                     FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                         "Nintendo "
                         "3DS/00000000000000000000000000000000/00000000000000000000000000000000/"
                         "extdata/00000000/{}");
    ProcessDirectory(0x00048000, fmt::format("{}extdata/00048000/", config.nand_data_path),
                     FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                         "data/00000000000000000000000000000000/extdata/00048000/{}");
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
    const auto CheckContent = [&out](u64 id, const std::string& var_path,
                                     const std::string& citra_path,
                                     const std::string& display_name) {
        if (!var_path.empty()) {
            out.push_back({ContentType::Sysdata, id, FileUtil::Exists(citra_path),
                           FileUtil::GetSize(var_path), display_name});
        }
    };

    {
        const auto sysdata_path = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir);
        CheckContent(0, config.bootrom_path, sysdata_path + BOOTROM9, BOOTROM9);
        CheckContent(3, config.secret_sector_path, sysdata_path + SECRET_SECTOR, SECRET_SECTOR);
        if (!config.bootrom_path.empty()) {
            // Check in case there was an older version
            const bool exists = FileUtil::Exists(sysdata_path + AES_KEYS) &&
                                FileUtil::GetSize(sysdata_path + AES_KEYS) >= 46 * 3;
            // 47 bytes = "slot0xIDKeyX=<32>\r\n" is only for Windows,
            // but it's maximum_size so probably okay
            out.push_back({ContentType::Sysdata, 4, exists, 47 * 3, AES_KEYS});
        }
        CheckContent(5, config.config_savegame_path,
                     fmt::format("{}data/00000000000000000000000000000000/sysdata/00010017/",
                                 FileUtil::GetUserPath(FileUtil::UserPath::NANDDir)),
                     "Config savegame");
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

void SDMCImporter::DeleteContent(const ContentSpecifier& specifier) const {
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
    case ContentType::SystemTitle:
    case ContentType::SystemApplet:
        return DeleteNandTitle(specifier.id);
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

void SDMCImporter::DeleteNandTitle(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}00000000000000000000000000000000/title/{:08x}/{:08x}/content/",
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteSavegame(u64 id) const {
    if ((id >> 32) == 0) { // NAND
        FileUtil::DeleteDirRecursively(
            fmt::format("{}data/00000000000000000000000000000000/sysdata/{:08x}/",
                        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id & 0xFFFFFFFF)));
    } else { // SDMC
        FileUtil::DeleteDirRecursively(fmt::format(
            "{}Nintendo "
            "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/{:08x}/"
            "{:08x}/data/",
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
    }
}

void SDMCImporter::DeleteExtdata(u64 id) const {
    if ((id >> 32) == 0) { // SDMC
        FileUtil::DeleteDirRecursively(fmt::format(
            "{}Nintendo "
            "3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/{:08x}/"
            "{:08x}/",
            FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
    } else { // NAND
        FileUtil::DeleteDirRecursively(fmt::format(
            "{}data/00000000000000000000000000000000/extdata/{:08x}/{:08x}/",
            FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF)));
    }
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
        LOAD_DATA(certs_db_path, CERTS_DB);
        LOAD_DATA(nand_title_db_path, TITLE_DB);
        LOAD_DATA(ticket_db_path, TICKET_DB);
        LOAD_DATA(safe_mode_firm_path, "firm/");
        LOAD_DATA(seed_db_path, SEED_DB);
        LOAD_DATA(secret_sector_path, SECRET_SECTOR);
        LOAD_DATA(config_savegame_path, "config.sav");
        LOAD_DATA(system_archives_path, "sysarchives/");
        LOAD_DATA(system_titles_path, "title/");
        LOAD_DATA(nand_data_path, "data/");
#undef LOAD_DATA

        // encTitleKeys.bin
        if (FileUtil::Exists(mount_point + "gm9/support/" ENC_TITLE_KEYS_BIN)) {
            config_template.enc_title_keys_bin_path =
                mount_point + "gm9/support/" ENC_TITLE_KEYS_BIN;
        }

        // Load version
        if (FileUtil::Exists(mount_point + "threeSD/version.txt")) {
            std::ifstream stream;
            OpenFStream(stream, mount_point + "threeSD/version.txt", std::ios::in);
            stream >> config_template.version;
        }
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
