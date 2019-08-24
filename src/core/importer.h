// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"

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
};

/**
 * Struct that specifies an importable content.
 */
struct ContentSpecifier {
    ContentType type;
    u64 id;
};

/**
 * A set of values that are used to initialize the importer.
 */
struct Config {
    std::string sdmc_path; ///< SDMC root path ("Nintendo 3DS/<ID0>/<ID1>")

    // Necessary system files keys are loaded from.
    std::string movable_sed_path; ///< Path to movable.sed
    std::string bootrom_path;     ///< Path to bootrom (boot9.bin)

    // The following system files are optional for importing and are only copied so that Citra
    // will be able to decrypt imported encrypted ROMs.
    std::string safe_mode_firm_path; ///< Path to safe mode firm
    std::string secret_sector_path;  ///< Path to secret sector (New3DS only)
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

private:
    bool ImportTitle(u64 id);
    bool ImportSavegame(u64 id);
    bool ImportExtdata(u64 id);

    Config config;
};
