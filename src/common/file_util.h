// Copyright 2013 Dolphin Emulator Project / 2014 Citra Emulator Project
//           2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>
#include "common/common_types.h"
#include "common/logging/log.h"
#ifdef _MSC_VER
#include "common/string_util.h"
#endif

namespace FileUtil {

// User paths for GetUserPath
enum class UserPath {
    CacheDir,
    CheatsDir,
    ConfigDir,
    DLLDir,
    LogDir,
    NANDDir,
    RootDir,
    SDMCDir,
    SysDataDir,
    UserDir,
};

// FileSystem tree node/
struct FSTEntry {
    bool isDirectory;
    u64 size;                 // file length or number of entries from children
    std::string physicalName; // name on disk
    std::string virtualName;  // name in FST names table
    std::vector<FSTEntry> children;
};

// Returns true if file filename exists
bool Exists(const std::string& filename);

// Returns true if filename is a directory
bool IsDirectory(const std::string& filename);

// Returns the size of filename (64bit)
u64 GetSize(const std::string& filename);

// Overloaded GetSize, accepts file descriptor
u64 GetSize(const int fd);

// Overloaded GetSize, accepts FILE*
u64 GetSize(FILE* f);

// Returns the size of a directory tree
u64 GetDirectoryTreeSize(const std::string& path, unsigned int recursion = 256);

// Returns true if successful, or path already exists.
bool CreateDir(const std::string& filename);

// Creates the full path of fullPath returns true on success
bool CreateFullPath(const std::string& fullPath);

// Deletes a given filename, return true on success
// Doesn't supports deleting a directory
bool Delete(const std::string& filename);

// Deletes a directory filename, returns true on success
bool DeleteDir(const std::string& filename);

// renames file srcFilename to destFilename, returns true on success
bool Rename(const std::string& srcFilename, const std::string& destFilename);

// copies file srcFilename to destFilename, returns true on success
bool Copy(const std::string& srcFilename, const std::string& destFilename);

// creates an empty file filename, returns true on success
bool CreateEmptyFile(const std::string& filename);

/**
 * @param num_entries_out to be assigned by the callable with the number of iterated directory
 * entries, never null
 * @param directory the path to the enclosing directory
 * @param virtual_name the entry name, without any preceding directory info
 * @return whether handling the entry succeeded
 */
using DirectoryEntryCallable = std::function<bool(
    u64* num_entries_out, const std::string& directory, const std::string& virtual_name)>;

/**
 * Scans a directory, calling the callback for each file/directory contained within.
 * If the callback returns failure, scanning halts and this function returns failure as well
 * @param num_entries_out assigned by the function with the number of iterated directory entries,
 * can be null
 * @param directory the directory to scan
 * @param callback The callback which will be called for each entry
 * @return whether scanning the directory succeeded
 */
bool ForeachDirectoryEntry(u64* num_entries_out, const std::string& directory,
                           DirectoryEntryCallable callback);

/**
 * Scans the directory tree, storing the results.
 * @param directory the parent directory to start scanning from
 * @param parent_entry FSTEntry where the filesystem tree results will be stored.
 * @param recursion Number of children directories to read before giving up.
 * @return the total number of files/directories found
 */
u64 ScanDirectoryTree(const std::string& directory, FSTEntry& parent_entry,
                      unsigned int recursion = 0);

// deletes the given directory and anything under it. Returns true on success.
bool DeleteDirRecursively(const std::string& directory, unsigned int recursion = 256);

// Returns the current directory
std::optional<std::string> GetCurrentDir();

// Create directory and copy contents (does not overwrite existing files)
void CopyDir(const std::string& source_path, const std::string& dest_path);

// Set the current directory to given directory
bool SetCurrentDir(const std::string& directory);

enum class UserPathType {
    Normal,   /// user-wide normal install
    Portable, /// portable install
    Flatpak,  /// Flatpak install (Linux only)
};

// Determine type of the user path that should be used.
UserPathType GetUserPathType();

// User paths are not directly used by us, and are only preserved here to match Citra's
// user folder and tell where to copy the files to.

void SetUserPath(const std::string& path = "");

// Returns a pointer to a string with a Citra data dir in the user's home
// directory. To be used in "multi-user" mode (that is, installed).
const std::string& GetUserPath(UserPath path);

// Returns the path to where the sys file are
std::string GetSysDirectory();

#ifdef __APPLE__
std::string GetBundleDirectory();
#endif

#ifdef _WIN32
const std::string& GetExeDirectory();
std::string AppDataRoamingDirectory();
#else
std::string GetXDGDirectory(const std::string& envvar);
#endif

std::size_t WriteStringToFile(bool text_file, const std::string& filename, std::string_view str);

std::size_t ReadFileToString(bool text_file, const std::string& filename, std::string& str);

/**
 * Splits the filename into 8.3 format
 * Loosely implemented following https://en.wikipedia.org/wiki/8.3_filename
 * @param filename The normal filename to use
 * @param short_name A 9-char array in which the short name will be written
 * @param extension A 4-char array in which the extension will be written
 */
void SplitFilename83(const std::string& filename, std::array<char, 9>& short_name,
                     std::array<char, 4>& extension);

// simple wrapper for cstdlib file functions to
// hopefully will make error checking easier
// and make forgetting an fclose() harder
class IOFile : public NonCopyable {
public:
    IOFile();

    // flags is used for windows specific file open mode flags, which
    // allows citra to open the logs in shared write mode, so that the file
    // isn't considered "locked" while citra is open and people can open the log file and view it
    IOFile(const std::string& filename, const char openmode[], int flags = 0);

    virtual ~IOFile();

    bool Open(const std::string& filename, const char openmode[], int flags = 0);
    bool Close();

    template <typename T>
    std::size_t ReadArray(T* data, std::size_t length) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return Read(reinterpret_cast<char*>(data), length * sizeof(T));
    }

    template <typename T>
    std::size_t WriteArray(const T* data, std::size_t length) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return Write(reinterpret_cast<const char*>(data), length * sizeof(T));
    }

    template <typename T>
    std::size_t ReadBytes(T* data, std::size_t length) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return ReadArray(reinterpret_cast<char*>(data), length);
    }

    template <typename T>
    std::size_t WriteBytes(const T* data, std::size_t length) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        return WriteArray(reinterpret_cast<const char*>(data), length);
    }

    template <typename T>
    std::size_t WriteObject(const T& object) {
        static_assert(!std::is_pointer_v<T>, "WriteObject arguments must not be a pointer");
        return WriteArray(&object, 1);
    }

    std::size_t WriteString(std::string_view str) {
        return WriteArray(str.data(), str.length());
    }

    virtual std::size_t Read(char* data, std::size_t length);
    virtual std::size_t Write(const char* data, std::size_t length);

    std::vector<u8> GetData();

    bool IsOpen() const {
        return nullptr != m_file;
    }

    // m_good is set to false when a read, write or other function fails
    bool IsGood() const {
        return m_good;
    }
    explicit operator bool() const {
        return IsGood();
    }

    virtual bool Seek(s64 off, int origin);
    u64 Tell() const;
    u64 GetSize() const;
    bool Resize(u64 size);
    bool Flush();

    // clear error state
    void Clear() {
        m_good = true;
        std::clearerr(m_file);
    }

private:
    std::FILE* m_file = nullptr;
    bool m_good = true;
};

template <typename T>
bool WriteBytesToFile(const std::string& path, T* data, std::size_t length) {
    if (!CreateFullPath(path)) {
        LOG_ERROR(Core, "Could not create path {}", path);
        return false;
    }

    IOFile file(path, "wb");
    if (!file.IsOpen()) {
        LOG_ERROR(Core, "Could not open file {}", path);
        return false;
    }
    if (file.WriteBytes(data, length) != length) {
        LOG_ERROR(Core, "Write data failed (file: {})", path);
        return false;
    }
    return true;
}

} // namespace FileUtil

// To deal with Windows being dumb at unicode:
template <typename T>
void OpenFStream(T& fstream, const std::string& filename, std::ios_base::openmode openmode) {
#ifdef _MSC_VER
    fstream.open(Common::UTF8ToUTF16W(filename), openmode);
#else
    fstream.open(filename, openmode);
#endif
}
