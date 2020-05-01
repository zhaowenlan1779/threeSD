// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "common/common_types.h"

namespace Core {

class SDMCDecryptor;

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
};

/**
 * Encryption type of an importable content.
 */
enum class EncryptionType {
    None,
    FixedKey,
    NCCHSecure1,
    NCCHSecure2,
    NCCHSecure3,
    NCCHSecure4,
};

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
    EncryptionType encryption = EncryptionType::None; ///< Only for NCCHs. Encryption scheme.
    bool seed_crypto = false; ///< Only for NCCHs. Whether seed crypto is used.
    std::vector<u16> icon;    ///< Optional. The content's icon.
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

    // The following system files are optional for importing and are only copied so that Citra
    // will be able to decrypt imported encrypted ROMs.

    std::string safe_mode_firm_path; ///< Path to safe mode firm (A folder) (Sysdata 1)
    std::string seed_db_path;        ///< Path to seeddb.bin (Sysdata 2)
    std::string secret_sector_path;  ///< Path to secret sector (New3DS only) (Sysdata 3)
    // Note: Sysdata 4 is aes_keys.txt (slot0x25KeyX)
    std::string config_savegame_path; ///< Path to config savegame (Sysdata 5)

    std::string system_archives_path; ///< Path to system archives.
};

class NCCHContainer;

class SDMCImporter {
public:
    /// (current_size, total_size)
    using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

    /**
     * Initializes the importer.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
     */
    explicit SDMCImporter(const Config& config);

    ~SDMCImporter();

    /**
     * Imports a specific content by its specifier.
     * Blocks, but can be aborted on another thread if needed.
     * @return true on success, false otherwise
     */
    bool ImportContent(const ContentSpecifier& specifier,
                       const ProgressCallback& callback = [](std::size_t, std::size_t) {});

    /**
     * Aborts current importing.
     */
    void AbortImporting();

    /**
     * Dumps a content to CXI.
     * Blocks, but can be aborted on another thread.
     * @return true on success, false otherwise
     */
    bool DumpCXI(const ContentSpecifier& specifier, const std::string& destination,
                 const ProgressCallback& callback = [](std::size_t, std::size_t) {});

    /**
     * Aborts current CXI dumping.
     */
    void AbortDumpCXI();

    /**
     * Deletes/Cleans up a content. Used for deleting contents that have
     * not been fully imported.
     */
    void DeleteContent(const ContentSpecifier& specifier);

    /**
     * Gets a list of dumpable content specifiers.
     */
    std::vector<ContentSpecifier> ListContent() const;

    /**
     * Returns whether the importer is in good state.
     */
    bool IsGood() const;

private:
    bool Init();

    bool ImportTitle(const ContentSpecifier& specifier, const ProgressCallback& callback);
    bool ImportSavegame(u64 id, const ProgressCallback& callback);
    bool ImportExtdata(u64 id, const ProgressCallback& callback);
    bool ImportSystemArchive(u64 id, const ProgressCallback& callback);
    bool ImportSysdata(u64 id, const ProgressCallback& callback);

    void ListTitle(std::vector<ContentSpecifier>& out) const;
    void ListExtdata(std::vector<ContentSpecifier>& out) const;
    void ListSystemArchive(std::vector<ContentSpecifier>& out) const;
    void ListSysdata(std::vector<ContentSpecifier>& out) const;

    void DeleteTitle(u64 id) const;
    void DeleteSavegame(u64 id) const;
    void DeleteExtdata(u64 id) const;
    void DeleteSystemArchive(u64 id) const;
    void DeleteSysdata(u64 id) const;

    /**
     * Loads the English short title name, extdata id, encryption and icon of a title.
     * @param path Path of the 'content' folder relative to the SDMC root folder.
     * Required to end with '/'.
     * @return {name, extdata_id, encryption, seed_crypto, icon}
     */
    std::tuple<std::string, u64, EncryptionType, bool, std::vector<u16>> LoadTitleData(
        const std::string& path) const;

    bool is_good{};
    Config config;
    std::unique_ptr<SDMCDecryptor> decryptor;

    // The NCCH used to dump CXIs.
    std::unique_ptr<NCCHContainer> dump_cxi_ncch;
};

/**
 * Look for and load preset config for a SD card mounted at mount_point.
 * @return a list of preset config available. can be empty
 */
std::vector<Config> LoadPresetConfig(std::string mount_point);

} // namespace Core
