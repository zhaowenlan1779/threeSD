// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/importer.h"
#include "core/key/key.h"

SDMCImporter::SDMCImporter(const Config& config_) : config(config_) {
    Key::LoadBootromKeys(config.bootrom_path);
    Key::LoadMovableSedKeys(config.movable_sed_path);
}

SDMCImporter::~SDMCImporter() = default;
