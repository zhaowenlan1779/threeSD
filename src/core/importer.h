// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "common/common_types.h"
#include "common/progress_callback.h"
#include "core/file_decryptor.h"
#include "core/file_sys/cia_common.h"

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
    Application,
    Update,
    DLC,
    Savegame,
    Extdata,
    SystemArchive,
    Sysdata,
    SystemTitle,
    SystemApplet, // This should belong to System Title, but they cause problems so a new category.
};

constexpr bool IsTitle(ContentType type) {
    return type == ContentType::Application || type == ContentType::Update ||
           type == ContentType::DLC || type == ContentType::SystemTitle ||
           type == ContentType::SystemApplet;
}
constexpr bool IsNandTitle(ContentType type) {
    return type == ContentType::SystemTitle || type == ContentType::SystemApplet;
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
    std::string sdmc_path; ///< SDMC root path ("Nintendo 3DS/<ID0>/<ID1>")
    std::string user_path; ///< Target user path of Citra

    // Necessary system files keys are loaded from.
    std::string movable_sed_path; ///< Path to movable.sed
    std::string bootrom_path;     ///< Path to bootrom (boot9.bin) (Sysdata 0)
    std::string certs_db_path;    ///< Path to certs.db. Used while building CIA.

    // Optional, used while building CIA, but usually missing these files won't hinder CIA building.
    std::string nand_title_db_path;      ///< Path to NAND title.db. Entirely optional.
    std::string ticket_db_path;          ///< Path to ticket.db. Entirely optional.
    std::string enc_title_keys_bin_path; ///< Path to encTitleKeys.bin. Entireley optional.

    // The following system files are optional for importing and are only copied so that Citra
    // will be able to decrypt imported encrypted ROMs.

    std::string safe_mode_firm_path; ///< Path to safe mode firm (A folder) (Sysdata 1)
    std::string seed_db_path;        ///< Path to seeddb.bin (Sysdata 2)
    std::string secret_sector_path;  ///< Path to secret sector (New3DS only) (Sysdata 3)
    // Note: Sysdata 4 is aes_keys.txt (slot0x25KeyX)
    std::string config_savegame_path; ///< Path to config savegame (Sysdata 5)

    std::string system_archives_path; ///< Path to system archives.
    std::string system_titles_path;   ///< Path to system titles.
    std::string nand_data_path;       ///< Path to NAND data. (Extdata and savedata)

    int version = 0; ///< Version of the dumper used.
};

// Version of the current dumper.
constexpr int CurrentDumperVersion = 4;

class SDMCFile;
class NCCHContainer;

class SDMCImporter {
public:
    /**
     * Initializes the importer.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
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
    std::shared_ptr<FileUtil::IOFile> OpenBootContent(const ContentSpecifier& specifier,
                                                      const TitleMetadata& tmd) const;

    std::shared_ptr<TicketDB>& GetTicketDB() {
        return ticket_db;
    }

    const std::shared_ptr<TicketDB>& GetTicketDB() const {
        return ticket_db;
    }

private:
    bool Init();

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
    bool ImportSystemArchive(u64 id, const Common::ProgressCallback& callback);
    bool ImportSysdata(u64 id, const Common::ProgressCallback& callback);

    void ListTitle(std::vector<ContentSpecifier>& out) const;
    void ListNandTitle(std::vector<ContentSpecifier>& out) const;
    void ListNandSavegame(std::vector<ContentSpecifier>& out) const;
    void ListExtdata(std::vector<ContentSpecifier>& out) const;
    void ListSystemArchive(std::vector<ContentSpecifier>& out) const;
    void ListSysdata(std::vector<ContentSpecifier>& out) const;

    void DeleteContent(const ContentSpecifier& specifier) const;
    void DeleteTitle(u64 id) const;
    void DeleteNandTitle(u64 id) const;
    void DeleteSavegame(u64 id) const;
    void DeleteExtdata(u64 id) const;
    void DeleteSystemArchive(u64 id) const;
    void DeleteSysdata(u64 id) const;

    bool is_good{};
    Config config;
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
 * @return a list of preset config available. can be empty
 */
std::vector<Config> LoadPresetConfig(std::string mount_point);

} // namespace Core
