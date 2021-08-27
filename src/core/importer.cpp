// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <regex>
#include <cryptopp/sha.h>
#include "common/assert.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/scope_exit.h"
#include "common/string_util.h"
#include "core/cia_builder.h"
#include "core/db/seed_db.h"
#include "core/db/title_db.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/data/data_container.h"
#include "core/file_sys/data/extdata.h"
#include "core/file_sys/data/savegame.h"
#include "core/file_sys/ncch_container.h"
#include "core/file_sys/smdh.h"
#include "core/file_sys/title_metadata.h"
#include "core/importer.h"
#include "core/key/key.h"
#include "core/sdmc_decryptor.h"

namespace Core {

SDMCImporter::SDMCImporter(const Config& config_) : config(config_) {
    is_good = Init();
}

SDMCImporter::~SDMCImporter() {
    // Unload global DBs
    Certs::Clear();
    Seeds::Clear();
}

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

    // Load global DBs
    if (!config.seed_db_path.empty()) {
        Seeds::Load(config.seed_db_path);
    }
    if (!config.certs_db_path.empty()) {
        Certs::Load(config.certs_db_path);
    }

    // Load Ticket DB
    if (!config.ticket_db_path.empty()) {
        ticket_db = std::make_shared<TicketDB>(config.ticket_db_path);
    }
    if (!ticket_db || !ticket_db->IsGood()) {
        LOG_WARNING(Core, "ticket.db not present or is invalid");
        ticket_db.reset();
    }

    // Create children
    sdmc_decryptor = std::make_unique<SDMCDecryptor>(config.sdmc_path);
    cia_builder = std::make_unique<CIABuilder>(config, ticket_db);

    // Load SDMC Title DB
    {
        DataContainer container(sdmc_decryptor->DecryptFile("/dbs/title.db"));
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
    sdmc_decryptor->Abort();
    file_decryptor.Abort();
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
    case ContentType::Title:
        return ImportTitle(specifier, callback);
    case ContentType::Savegame:
        return ImportSavegame(specifier.id, callback);
    case ContentType::NandSavegame:
        return ImportNandSavegame(specifier.id, callback);
    case ContentType::Extdata:
        return ImportExtdata(specifier.id, callback);
    case ContentType::NandExtdata:
        return ImportNandExtdata(specifier.id, callback);
    case ContentType::Sysdata:
        return ImportSysdata(specifier.id, callback);
    case ContentType::NandTitle:
        return ImportNandTitle(specifier, callback);
    default:
        UNREACHABLE();
    }
}

namespace {

using DecryptionFunc = std::function<bool(const std::string&, const Common::ProgressCallback&)>;
bool ImportTitleGeneric(const std::string& base_path, const ContentSpecifier& specifier,
                        const Common::ProgressCallback& callback,
                        const DecryptionFunc& decryption_func) {

    Common::ProgressCallbackWrapper wrapper{specifier.maximum_size};
    const FileUtil::DirectoryEntryCallable DirectoryEntryCallback =
        [size = base_path.size(), &DirectoryEntryCallback, &callback, &decryption_func,
         &wrapper](u64* /*num_entries_out*/, const std::string& directory,
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
            return decryption_func(filepath, wrapper.Wrap(callback));
        };
    const auto path = fmt::format("title/{:08x}/{:08x}/content/", (specifier.id >> 32),
                                  (specifier.id & 0xFFFFFFFF));
    return FileUtil::ForeachDirectoryEntry(nullptr, base_path + path, DirectoryEntryCallback);
}

} // namespace

bool SDMCImporter::ImportTitle(const ContentSpecifier& specifier,
                               const Common::ProgressCallback& callback) {
    return ImportTitleGeneric(
        config.sdmc_path, specifier, callback,
        [this](const std::string& filepath, const Common::ProgressCallback& wrapped_callback) {
            return sdmc_decryptor->DecryptAndWriteFile(
                filepath,
                FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                    "Nintendo "
                    "3DS/00000000000000000000000000000000/00000000000000000000000000000000" +
                    filepath,
                wrapped_callback);
        });
}

bool SDMCImporter::ImportNandTitle(const ContentSpecifier& specifier,
                                   const Common::ProgressCallback& callback) {

    const auto base_path =
        config.system_titles_path.substr(0, config.system_titles_path.size() - 6);
    return ImportTitleGeneric(
        base_path, specifier, callback,
        [this, &base_path](const std::string& filepath,
                           const Common::ProgressCallback& wrapped_callback) {
            const auto physical_path = base_path + filepath.substr(1);
            const auto citra_path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                    "00000000000000000000000000000000" + filepath;
            if (!FileUtil::CreateFullPath(citra_path)) {
                LOG_ERROR(Core, "Could not create path {}", citra_path);
                return false;
            }
            // Crypto is not set: plain copy with progress.
            return file_decryptor.CryptAndWriteFile(
                std::make_shared<FileUtil::IOFile>(physical_path, "rb"),
                FileUtil::GetSize(physical_path),
                std::make_shared<FileUtil::IOFile>(citra_path, "wb"), wrapped_callback);
        });
}

bool SDMCImporter::ImportSavegame(u64 id,
                                  [[maybe_unused]] const Common::ProgressCallback& callback) {
    const auto path = fmt::format("title/{:08x}/{:08x}/data/", (id >> 32), (id & 0xFFFFFFFF));

    DataContainer container(sdmc_decryptor->DecryptFile(fmt::format("/{}00000001.sav", path)));
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
    Extdata extdata("/" + path, *sdmc_decryptor);
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
    case 1: { // seed db
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
    case 2: { // secret sector
        const auto target_path =
            FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SECRET_SECTOR;
        LOG_INFO(Core, "Copying {} from {} to {}", SECRET_SECTOR, config.secret_sector_path,
                 target_path);
        if (!FileUtil::CreateFullPath(target_path)) {
            return false;
        }
        return FileUtil::Copy(config.secret_sector_path, target_path);
    }
    case 3: { // aes_keys.txt
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
    const bool is_nand = type == ContentType::NandTitle;

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
        return out.Load(file.GetData());
    } else {
        return out.Load(sdmc_decryptor->DecryptFile(tmd_path.substr(config.sdmc_path.size() - 1)));
    }
}

bool SDMCImporter::LoadTMD(const ContentSpecifier& specifier, TitleMetadata& out) const {
    return LoadTMD(specifier.type, specifier.id, out);
}

std::shared_ptr<FileUtil::IOFile> SDMCImporter::OpenContent(const ContentSpecifier& specifier,
                                                            u32 content_id) const {
    if (specifier.type == ContentType::NandTitle) {
        const auto path =
            fmt::format("{}{:08x}/{:08x}/content/{:08x}.app", config.system_titles_path,
                        (specifier.id >> 32), (specifier.id & 0xFFFFFFFF), content_id);
        return std::make_shared<FileUtil::IOFile>(path, "rb");
    } else {
        // For DLCs, there one subfolder every 256 titles, but in practice hardcoded 00000000
        // should be fine (also matches GodMode9 behaviour)
        const auto format_str = (specifier.id >> 32) == 0x0004008c
                                    ? "/title/{:08x}/{:08x}/content/00000000/{:08x}.app"
                                    : "/title/{:08x}/{:08x}/content/{:08x}.app";
        const auto path =
            fmt::format(format_str, (specifier.id >> 32), (specifier.id & 0xFFFFFFFF), content_id);
        return std::make_shared<SDMCFile>(config.sdmc_path, path, "rb");
    }
}

struct TitleData {
    std::string name;
    u64 extdata_id;
    std::vector<u16> icon;
};
static TitleData LoadTitleData(NCCHContainer& ncch) {
    static const std::unordered_map<u64, const char*> NamedTitles{{
        // System Applications (to avoid confusion)
        {0x00040010'2002c800, "New 3DS HOME Menu manual (JPN)"},
        {0x00040010'2002cf00, "New 3DS HOME Menu manual (USA)"},
        {0x00040010'2002d000, "New 3DS HOME Menu manual (EUR)"},
        {0x00040010'2002d700, "New 3DS HOME Menu manual (KOR)"},
        {0x00040010'2002c900, "New 3DS Friend List manual (JPN)"},
        {0x00040010'2002d100, "New 3DS Friend List manual (USA)"},
        {0x00040010'2002d200, "New 3DS Friend List manual (EUR)"},
        {0x00040010'2002d800, "New 3DS Friend List manual (KOR)"},
        {0x00040010'2002ca00, "New 3DS Notifications manual (JPN)"},
        {0x00040010'2002d300, "New 3DS Notifications manual (USA)"},
        {0x00040010'2002d400, "New 3DS Notifications manual (EUR)"},
        {0x00040010'2002d900, "New 3DS Notifications manual (KOR)"},
        {0x00040010'2002cb00, "New 3DS Game Notes manual (JPN)"},
        {0x00040010'2002d500, "New 3DS Game Notes manual (USA)"},
        {0x00040010'2002d600, "New 3DS Game Notes manual (EUR)"},
        {0x00040010'2002da00, "New 3DS Game Notes manual (KOR)"},
        // System Archives
        {0x0004001b'00010002, "ClCertA"},
        {0x0004009b'00010202, "Mii Data"},
        {0x0004009b'00010402, "Region Manifest"},
        {0x0004009b'00014002, "Shared Font (JPN/EUR/USA)"},
        {0x0004009b'00014102, "Shared Font (CHN)"},
        {0x0004009b'00014202, "Shared Font (KOR)"},
        {0x0004009b'00014302, "Shared Font (TWN)"},
        {0x000400db'00010302, "NGWord Bad word list"},
    }};

    u64 program_id{};
    ncch.ReadProgramId(program_id);

    u64 extdata_id{};
    ncch.ReadExtdataId(extdata_id);

    // Determine title name from codeset name
    std::string codeset_name;
    ncch.ReadCodesetName(codeset_name);

    std::string title_name;
    if (!codeset_name.empty()) {
        title_name = fmt::format("{} (0x{:016x})", std::move(codeset_name), program_id);
    }
    if (NamedTitles.count(program_id)) { // Override name
        title_name = NamedTitles.at(program_id);
    }

    // Load SMDH (for name and icon)
    std::vector<u8> smdh_buffer;
    if (!ncch.LoadSectionExeFS("icon", smdh_buffer)) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS");
        return TitleData{std::move(title_name), extdata_id};
    }

    if (smdh_buffer.size() != sizeof(SMDH)) {
        LOG_ERROR(Core, "ExeFS icon section size is not correct");
        return TitleData{std::move(title_name), extdata_id};
    }

    SMDH smdh;
    std::memcpy(&smdh, smdh_buffer.data(), smdh_buffer.size());
    if (!NamedTitles.count(program_id)) { // Name was not overridden
        title_name = Common::UTF16BufferToUTF8(smdh.GetShortTitle(SMDH::TitleLanguage::English));
    }
    return TitleData{std::move(title_name), extdata_id, smdh.GetIcon(false)};
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
    if (!ncch.LoadSectionExeFS("icon", smdh_buffer) || smdh_buffer.size() != sizeof(SMDH)) {
        LOG_WARNING(Core, "Failed to load icon in ExeFS or size incorrect");
        return NormalizeFilename(
            fmt::format("{:016X} {} ({})", program_id, codeset_name, product_code));
    } else {
        SMDH smdh;
        std::memcpy(&smdh, smdh_buffer.data(), smdh_buffer.size());
        const auto short_title =
            Common::UTF16BufferToUTF8(smdh.GetShortTitle(SMDH::TitleLanguage::English));
        return NormalizeFilename(fmt::format("{:016X} {} ({}) ({})", program_id, short_title,
                                             product_code, smdh.GetRegionString()));
    }
}

bool SDMCImporter::DumpCXI(const ContentSpecifier& specifier, std::string destination,
                           const Common::ProgressCallback& callback, bool auto_filename) {

    // not an Application
    if (specifier.type != ContentType::Title || (specifier.id >> 32) != 0x00040000) {
        LOG_ERROR(Core, "Unsupported specifier (id={:016x})", specifier.id);
        return false;
    }

    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }

    dump_cxi_ncch = std::make_unique<NCCHContainer>(OpenContent(specifier, tmd.GetBootContentID()));

    if (destination.back() == '/' || destination.back() == '\\') {
        auto_filename = true;
    }
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

    if (!dump_cxi_ncch->DecryptToFile(std::make_shared<FileUtil::IOFile>(destination, "wb"),
                                      callback)) {
        FileUtil::Delete(destination);
        return false;
    }
    return true;
}

void SDMCImporter::AbortDumpCXI() {
    dump_cxi_ncch->AbortDecryptToFile();
}

bool SDMCImporter::CanBuildLegitCIA(const ContentSpecifier& specifier) const {
    if (!IsTitle(specifier.type)) {
        return false;
    }

    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }
    if (!tmd.VerifyHashes() || !tmd.ValidateSignature()) {
        return false;
    }
    // TODO: check ticket, etc?
    return true;
}

bool SDMCImporter::BuildCIA(CIABuildType build_type, const ContentSpecifier& specifier,
                            std::string destination, const Common::ProgressCallback& callback,
                            bool auto_filename) {

    if (!Certs::IsLoaded()) {
        LOG_ERROR(Core, "Missing certs");
        return false;
    }

    if (!IsTitle(specifier.type)) {
        LOG_ERROR(Core, "Unsupported specifier type {}", static_cast<int>(specifier.type));
        return false;
    }

    // Load TMD
    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }

    if (destination.back() == '/' || destination.back() == '\\') {
        auto_filename = true;
    }
    static constexpr std::array<std::string_view, 3> BuildTypeExts{{
        "standard.cia",
        "piratelegit.cia",
        "legit.cia",
    }};
    if (auto_filename) {
        if (destination.back() != '/' && destination.back() != '\\') {
            destination.push_back('/');
        }
        auto file = OpenContent(specifier, tmd.GetBootContentID());
        if (!file) {
            LOG_ERROR(Core, "Could not open boot content");
            return false;
        }
        NCCHContainer ncch(std::move(file));
        const auto filename =
            fmt::format("{} (v{}).{}", GetTitleFileName(ncch), tmd.GetTitleVersionString(),
                        BuildTypeExts.at(static_cast<std::size_t>(build_type)));
        destination.append(filename);
    }

    bool ret = cia_builder->Init(build_type, destination, tmd, specifier.maximum_size, callback);
    SCOPE_EXIT({
        cia_builder->Cleanup();
        if (!ret) { // Remove borked file
            FileUtil::Delete(destination);
        }
    });
    if (!ret) {
        return false;
    }

    for (const auto& tmd_chunk : tmd.tmd_chunks) {
        auto file = OpenContent(specifier, tmd_chunk.id);
        if (!file) {
            if (static_cast<u16>(tmd_chunk.type) & 0x4000) { // optional
                continue;
            }
            LOG_ERROR(Core, "Could not open content {:08x}", static_cast<u32>(tmd_chunk.id));
            ret = false;
            return false;
        }

        NCCHContainer ncch(std::move(file));
        ret = cia_builder->AddContent(tmd_chunk.id, ncch);
        if (!ret) {
            return false;
        }
    }

    ret = cia_builder->Finalize();
    return ret;
}

void SDMCImporter::AbortBuildCIA() {
    cia_builder->Abort();
}

// Removed actual writing of the data
class HashOnlyFile : public FileUtil::IOFile {
public:
    explicit HashOnlyFile() = default;
    ~HashOnlyFile() override = default;

    std::size_t Write(const char* data, std::size_t length) override {
        sha.Update(reinterpret_cast<const CryptoPP::byte*>(data), length);
        return length;
    }

    bool VerifyHash(const u8* hash) {
        const bool ret = sha.Verify(hash);
        sha.Restart();
        return ret;
    }

private:
    CryptoPP::SHA256 sha;
};

bool SDMCImporter::CheckTitleContents(const ContentSpecifier& specifier,
                                      const Common::ProgressCallback& callback) {

    if (!IsTitle(specifier.type)) {
        LOG_ERROR(Core, "Unsupported specifier type {}", static_cast<int>(specifier.type));
        return false;
    }

    // Load TMD
    TitleMetadata tmd;
    if (!LoadTMD(specifier.type, specifier.id, tmd)) {
        return false;
    }

    Common::ProgressCallbackWrapper wrapper{specifier.maximum_size};

    for (const auto& tmd_chunk : tmd.tmd_chunks) {
        auto file = OpenContent(specifier, tmd_chunk.id);
        if (!file) {
            if (static_cast<u16>(tmd_chunk.type) & 0x4000) { // optional
                continue;
            }
            LOG_INFO(Core, "Could not open content {:08x}", static_cast<u32>(tmd_chunk.id));
            return false;
        }

        std::shared_ptr<HashOnlyFile> dest_file = std::make_shared<HashOnlyFile>();
        if (!file_decryptor.CryptAndWriteFile(file, file->GetSize(), dest_file,
                                              wrapper.Wrap(callback))) {
            return false;
        }
        if (!dest_file->VerifyHash(tmd_chunk.hash.data())) {
            LOG_INFO(Core, "Hash dismatch for content {:08x}", static_cast<u32>(tmd_chunk.id));
            return false;
        }
    }

    callback(specifier.maximum_size, specifier.maximum_size);
    return true;
}

// Add a certain amount to the titles' maximum sizes, so that they are always larger than CIA sizes
constexpr u64 TitleSizeAllowance = 0xA000;

void SDMCImporter::ListTitle(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [this, &out, &sdmc_path = config.sdmc_path](u64 high_id) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, fmt::format("{}title/{:08x}/", sdmc_path, high_id),
            [this, &sdmc_path, high_id, &out](u64* /*num_entries_out*/,
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
                        if (!LoadTMD(ContentType::Title, id, tmd)) {
                            out.push_back({ContentType::Title, id,
                                           FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(directory + virtual_name +
                                                                          "/content/")});
                            break;
                        }

                        const auto boot_content_path =
                            fmt::format("/title/{:08x}/{}/content/{:08x}.app", high_id,
                                        virtual_name, tmd.GetBootContentID());
                        NCCHContainer ncch(
                            std::make_shared<SDMCFile>(sdmc_path, boot_content_path, "rb"));
                        if (!ncch.Load()) {
                            LOG_WARNING(Core, "Could not load NCCH {}", boot_content_path);
                            out.push_back({ContentType::Title, id,
                                           FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(directory + virtual_name +
                                                                          "/content/")});
                            break;
                        }

                        const auto& [name, extdata_id, icon] = LoadTitleData(ncch);
                        const auto size =
                            FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/content/") +
                            TitleSizeAllowance;
                        out.push_back({ContentType::Title, id,
                                       FileUtil::Exists(citra_path + "content/"), size, name,
                                       extdata_id, icon});
                    } while (false);
                }

                if (high_id != 0x00040000) { // Check savegame only for applications
                    return true;
                }
                if (FileUtil::Exists(directory + virtual_name + "/data/")) {
                    // Savegames can be uninitialized.
                    // TODO: Is there a better way of checking this other than performing the
                    // decryption? (Very costy)
                    DataContainer container(sdmc_decryptor->DecryptFile(
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

    ProcessDirectory(0x00040000);
    ProcessDirectory(0x0004000e);
    ProcessDirectory(0x0004008c);
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
                        if (!LoadTMD(ContentType::NandTitle, id, tmd)) {
                            out.push_back({ContentType::NandTitle, id,
                                           FileUtil::Exists(citra_path + "content/"),
                                           FileUtil::GetDirectoryTreeSize(content_path)});
                            break;
                        }

                        const auto boot_content_path =
                            fmt::format("{}{:08x}.app", content_path, tmd.GetBootContentID());
                        NCCHContainer ncch(
                            std::make_shared<FileUtil::IOFile>(boot_content_path, "rb"));
                        if (!ncch.Load()) {
                            LOG_WARNING(Core, "Could not load NCCH {}", boot_content_path);
                            break;
                        }

                        const auto& [name, extdata_id, icon] = LoadTitleData(ncch);
                        const auto size =
                            FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/content/") +
                            TitleSizeAllowance;
                        out.push_back({ContentType::NandTitle, id,
                                       FileUtil::Exists(citra_path + "content/"), size, name,
                                       extdata_id, icon});
                    } while (false);
                }
                return true;
            });
    };

    ProcessDirectory(0x00040010);
    ProcessDirectory(0x0004001b);
    ProcessDirectory(0x00040030);
    ProcessDirectory(0x0004009b);
    ProcessDirectory(0x000400db);
    ProcessDirectory(0x00040130);
    ProcessDirectory(0x00040138);
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
            out.push_back({ContentType::NandSavegame, id, FileUtil::Exists(citra_path),
                           FileUtil::GetSize(path)});
            return true;
        });
}

void SDMCImporter::ListExtdata(std::vector<ContentSpecifier>& out) const {
    const auto ProcessDirectory = [&out](u64 id_high, ContentType type, const std::string& path,
                                         const std::string& citra_path_template) {
        FileUtil::ForeachDirectoryEntry(
            nullptr, path,
            [&out, id_high, type, citra_path_template](u64* /*num_entries_out*/,
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
                out.push_back({type, (id_high << 32) | id, FileUtil::Exists(citra_path),
                               FileUtil::GetDirectoryTreeSize(directory + virtual_name + "/")});
                return true;
            });
    };
    ProcessDirectory(0, ContentType::Extdata, fmt::format("{}extdata/00000000/", config.sdmc_path),
                     FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                         "Nintendo "
                         "3DS/00000000000000000000000000000000/00000000000000000000000000000000/"
                         "extdata/00000000/{}");
    ProcessDirectory(0x00048000, ContentType::NandExtdata,
                     fmt::format("{}extdata/00048000/", config.nand_data_path),
                     FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                         "data/00000000000000000000000000000000/extdata/00048000/{}");
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
        CheckContent(2, config.secret_sector_path, sysdata_path + SECRET_SECTOR, SECRET_SECTOR);
        if (!config.bootrom_path.empty()) {
            // Check in case there was an older version
            const bool exists = FileUtil::Exists(sysdata_path + AES_KEYS) &&
                                FileUtil::GetSize(sysdata_path + AES_KEYS) >= 46 * 3;
            // 47 bytes = "slot0xIDKeyX=<32>\r\n" is only for Windows,
            // but it's maximum_size so probably okay
            out.push_back({ContentType::Sysdata, 3, exists, 47 * 3, AES_KEYS});
        }
    }

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
        {ContentType::Sysdata, 1, exists, FileUtil::GetSize(config.seed_db_path), SEED_DB});
}

void SDMCImporter::DeleteContent(const ContentSpecifier& specifier) const {
    switch (specifier.type) {
    case ContentType::Title:
        return DeleteTitle(specifier.id);
    case ContentType::Savegame:
        return DeleteSavegame(specifier.id);
    case ContentType::NandSavegame:
        return DeleteNandSavegame(specifier.id);
    case ContentType::Extdata:
        return DeleteExtdata(specifier.id);
    case ContentType::NandExtdata:
        return DeleteNandExtdata(specifier.id);
    case ContentType::Sysdata:
        return DeleteSysdata(specifier.id);
    case ContentType::NandTitle:
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
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}Nintendo "
        "3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/{:08x}/"
        "{:08x}/data/",
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteNandSavegame(u64 id) const {
    FileUtil::DeleteDirRecursively(
        fmt::format("{}data/00000000000000000000000000000000/sysdata/{:08x}/",
                    FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteExtdata(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}Nintendo "
        "3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/{:08x}/"
        "{:08x}/",
        FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteNandExtdata(u64 id) const {
    FileUtil::DeleteDirRecursively(fmt::format(
        "{}data/00000000000000000000000000000000/extdata/{:08x}/{:08x}/",
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir), (id >> 32), (id & 0xFFFFFFFF)));
}

void SDMCImporter::DeleteSysdata(u64 id) const {
    switch (id) {
    case 0: { // boot9.bin
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + BOOTROM9);
    }
    case 1: { // seed db
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SEED_DB);
    }
    case 2: { // secret sector
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + SECRET_SECTOR);
    }
    case 3: { // aes_keys.txt
        FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + AES_KEYS);
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
        LOAD_DATA(seed_db_path, SEED_DB);
        LOAD_DATA(secret_sector_path, SECRET_SECTOR);
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
