// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

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
    Sysdata,
};

/**
 * Struct that specifies an importable content.
 */
struct ContentSpecifier {
    ContentType type;
    u64 id;
    bool already_exists; ///< Tells whether a file already exists in target path.
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
};

class SDMCImporter {
public:
    /**
     * Initializes the importer.
     * @param root_folder Path to the "Nintendo 3DS/<ID0>/<ID1>" folder.
     */
    explicit SDMCImporter(const Config& config);

    ~SDMCImporter();

    /**
     * Dumps a specific content by its specifier.
     * @return true on success, false otherwise
     */
    bool ImportContent(const ContentSpecifier& specifier);

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
    bool ImportTitle(u64 id);
    bool ImportSavegame(u64 id);
    bool ImportExtdata(u64 id);
    bool ImportSysdata(u64 id);
    void ListTitle(std::vector<ContentSpecifier>& out) const;
    void ListExtdata(std::vector<ContentSpecifier>& out) const;
    void ListSysdata(std::vector<ContentSpecifier>& out) const;

    bool is_good{};
    Config config;
    std::unique_ptr<SDMCDecryptor> decryptor;
};

/**
 * Look for and load preset config for a SD card mounted at mount_point.
 * @return a list of preset config available. can be empty
 */
std::vector<Config> LoadPresetConfig(std::string mount_point);

} // namespace Core
