// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "common/common_types.h"
#include "common/progress_callback.h"
#include "core/file_decryptor.h"
#include "core/file_sys/cia_common.h"
#include "core/file_sys/smdh.h"

namespace Core {

class CIABuilder;
class SDMCDecryptor;
class TicketDB;
class TitleDB;
class TitleMetadata;

/**
 * Type of an importable content.
 * Applications, updates and DLCs are all considered titles.
 */
enum class ContentType {
    Title,
    Savegame,
    NandSavegame,
    Extdata,
    NandExtdata,
    Sysdata,
    NandTitle,
};
constexpr std::size_t ContentTypeCount = 7;

constexpr bool IsTitle(ContentType type) {
    return type == ContentType::Title || type == ContentType::NandTitle;
}

/**
 * Struct that specifies an importable content.
 */
struct ContentSpecifier {
    ContentType type;
    u64 id;
    bool already_exists; ///< Tells whether a file already exists in target path.
    u64 maximum_size; ///< The maximum size of the content. May be slightly bigger than real size.
    std::string name; ///< Optional. The content's preferred display name.
    u64 extdata_id;   ///< Extdata ID for Applications.
    std::vector<u16> icon; ///< Optional. The content's icon.
};

/**
 * A set of values that are used to initialize the importer.
 * All paths to directories shall end with a '/' (will be automatically added when not present)
 */
struct Config {
    int version = 0; ///< Version of the dumper used.

    std::string user_path; ///< Target user path of Citra
    std::string sdmc_path; ///< SDMC root path ("Nintendo 3DS/<ID0>/<ID1>")
    std::string id0;       ///< ID0 of the SDMC used in this configuration.

    // Necessary system files
    std::string bootrom_path;            ///< Path to bootrom (boot9.bin) (Sysdata 0)
    std::string secret_sector_path;      ///< Path to secret sector (New3DS only) (Sysdata 2)
    std::string enc_title_keys_bin_path; ///< Path to encTitleKeys.bin. Entirely optional.

    struct NandConfig {
        std::string nand_name;        ///< Name of the NAND used in this configuration.
        std::string movable_sed_path; ///< Path to movable.sed

        std::string certs_db_path;  ///< Path to certs.db. Used while building CIA.
        std::string ticket_db_path; ///< Path to ticket.db. Entirely optional.
        std::string title_db_path;  ///< Path to NAND title.db. Entirely optional.
        std::string seed_db_path;   ///< Path to seeddb.bin

        std::string title_path; ///< Path to system titles.
        std::string data_path;  ///< Path to NAND data. (Extdata and savedata)
    };
    /// A list of NandConfigs with the same ID0 (linked NANDs).
    /// The order of the NANDs matter: the importer will merge the dbs, but will only load NAND
    /// titles and data from the *first* NAND.
    std::vector<NandConfig> nands;
};

constexpr std::string_view SysNANDName = "Sys";
constexpr std::string_view EmuNANDPrefix = "Emu";

// Version of the current dumper.
constexpr int CurrentDumperVersion = 4;

constexpr bool IsConfigGood(const Config& config) {
    return config.version == CurrentDumperVersion && !config.user_path.empty() &&
           !config.sdmc_path.empty() && !config.bootrom_path.empty() && !config.nands.empty() &&
           !config.nands[0].movable_sed_path.empty();
}

constexpr bool IsConfigComplete(const Config& config) {
    // We are skipping the DBs here. Need more work regarding that
    return IsConfigGood(config) && !config.nands[0].title_path.empty() &&
           !config.nands[0].data_path.empty();
}

class SDMCFile;
class NCCHContainer;

class SDMCImporter {
public:
    /**
     * Initializes the importer.
     */
    explicit SDMCImporter(const Config& config);

    ~SDMCImporter();

    /**
     * Imports a specific content by its specifier, deleting it when failed.
     * Blocks, but can be aborted on another thread if needed.
     * @return true on success, false otherwise
     */
    bool ImportContent(
        const ContentSpecifier& specifier,
        const Common::ProgressCallback& callback = [](u64, u64) {});

    /**
     * Aborts current importing.
     */
    void AbortImporting();

    /**
     * Dumps a content to CXI.
     * Blocks, but can be aborted on another thread.
     * @return true on success, false otherwise
     */
    bool DumpCXI(const ContentSpecifier& specifier, std::string destination,
                 const Common::ProgressCallback& callback, bool auto_filename = false);

    /**
     * Aborts current CXI dumping.
     */
    void AbortDumpCXI();

    /**
     * Builds a CIA from a content.
     * Blocks, but can be aborted on another thread.
     * @return true on success, false otherwise
     */
    bool BuildCIA(CIABuildType build_type, const ContentSpecifier& specifier,
                  std::string destination, const Common::ProgressCallback& callback,
                  bool auto_filename = false);

    /**
     * Checks if a content can be built as a legit CIA.
     */
    bool CanBuildLegitCIA(const ContentSpecifier& specifier) const;

    /**
     * Aborts current CIA building
     */
    void AbortBuildCIA();

    /**
     * Checks the contents of a title against its TMD hashes.
     */
    bool CheckTitleContents(
        const ContentSpecifier& specifier,
        const Common::ProgressCallback& callback = [](u64, u64) {});

    /**
     * Gets a list of dumpable content specifiers.
     */
    std::vector<ContentSpecifier> ListContent() const;

    /**
     * Returns whether the importer is in good state.
     */
    bool IsGood() const;

    bool LoadTMD(ContentType type, u64 id, TitleMetadata& out) const;
    bool LoadTMD(const ContentSpecifier& specifier, TitleMetadata& out) const;

    std::string GetTitleContentsPath(const ContentSpecifier& specifier) const;
    std::shared_ptr<FileUtil::IOFile> OpenContent(const ContentSpecifier& specifier,
                                                  u32 content_id) const;

    std::shared_ptr<TicketDB>& GetTicketDB() {
        return ticket_db;
    }

    const std::shared_ptr<TicketDB>& GetTicketDB() const {
        return ticket_db;
    }

    SMDH::TitleLanguage GetSystemLanguage() const {
        return system_language;
    }

private:
    bool Init();
    void LoadSystemLanguage();

    // Impl of ImportContent without deleting mechanism.
    bool ImportContentImpl(
        const ContentSpecifier& specifier,
        const Common::ProgressCallback& callback = [](u64, u64) {});
    bool ImportTitle(const ContentSpecifier& specifier, const Common::ProgressCallback& callback);
    bool ImportNandTitle(const ContentSpecifier& specifier,
                         const Common::ProgressCallback& callback);
    bool ImportSavegame(u64 id, const Common::ProgressCallback& callback);
    bool ImportNandSavegame(u64 id, const Common::ProgressCallback& callback);
    bool ImportExtdata(u64 id, const Common::ProgressCallback& callback);
    bool ImportNandExtdata(u64 id, const Common::ProgressCallback& callback);
    bool ImportSysdata(u64 id, const Common::ProgressCallback& callback);

    void ListTitle(std::vector<ContentSpecifier>& out) const;
    void ListNandTitle(std::vector<ContentSpecifier>& out) const;
    void ListNandSavegame(std::vector<ContentSpecifier>& out) const;
    void ListExtdata(std::vector<ContentSpecifier>& out) const;
    void ListSysdata(std::vector<ContentSpecifier>& out) const;

    void DeleteContent(const ContentSpecifier& specifier) const;
    void DeleteTitle(u64 id) const;
    void DeleteNandTitle(u64 id) const;
    void DeleteSavegame(u64 id) const;
    void DeleteNandSavegame(u64 id) const;
    void DeleteExtdata(u64 id) const;
    void DeleteNandExtdata(u64 id) const;
    void DeleteSysdata(u64 id) const;

    bool is_good{};
    Config config;
    Config::NandConfig nand_config; // Main NAND config
    // System language, determined from config savegame. Used to return the title's names.
    SMDH::TitleLanguage system_language{SMDH::TitleLanguage::English};

    std::unique_ptr<SDMCDecryptor> sdmc_decryptor;
    FileDecryptor file_decryptor;

    // Used for CIA building
    std::unique_ptr<CIABuilder> cia_builder;
    std::shared_ptr<TicketDB> ticket_db;

    // The NCCH used to dump CXIs.
    std::unique_ptr<NCCHContainer> dump_cxi_ncch;

    std::unique_ptr<TitleDB> sdmc_title_db{};
    std::unique_ptr<TitleDB> nand_title_db{};
};

/**
 * Look for and load preset config for a SD card mounted at mount_point.
 * Note: This returns only one config per ID0.
 *       The frontend should allow the user to change the order of the NANDs.
 *
 * @return a list of preset config available. can be empty
 */
std::vector<Config> LoadPresetConfig(std::string mount_point);

} // namespace Core
