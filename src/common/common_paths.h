// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
//           2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Directory separators, do we need this?
#define DIR_SEP "/"
#define DIR_SEP_CHR '/'

// Citra's path names

// The user data dir
#define ROOT_DIR "."
#define USERDATA_DIR "user"
#ifdef USER_DIR
#define EMU_DATA_DIR USER_DIR
#else
#ifdef _WIN32
#define EMU_DATA_DIR "Citra"
#else
#define EMU_DATA_DIR "citra-emu"
#endif
#endif
#define CITRA_EXECUTABLE "citra-qt"

// Subdirs in the User dir returned by GetUserPath(UserPath::UserDir)
#define CONFIG_DIR "config"
#define CACHE_DIR "cache"
#define SDMC_DIR "sdmc"
#define NAND_DIR "nand"
#define SYSDATA_DIR "sysdata"
#define LOG_DIR "log"
#define CHEATS_DIR "cheats"
#define DLL_DIR "external_dlls"

#define BOOTROM9 "boot9.bin"
#define SECRET_SECTOR "sector0x96.bin"
#define MOVABLE_SED "movable.sed"
#define SEED_DB "seeddb.bin"
#define AES_KEYS "aes_keys.txt"
#define CERTS_DB "certs.db"
#define TITLE_DB "title.db"
#define TICKET_DB "ticket.db"
#define ENC_TITLE_KEYS_BIN "encTitleKeys.bin"
