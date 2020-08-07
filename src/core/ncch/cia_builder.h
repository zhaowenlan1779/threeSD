// Copyright 2017 Citra Emulator Project / 2020 Pengfei Zhu
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <mutex>
#include <string>
#include "common/file_util.h"
#include "common/swap.h"
#include "core/ncch/ncch_container.h"
#include "core/ncch/title_metadata.h"
#include "core/quick_decryptor.h"

namespace Core {

constexpr std::size_t CIA_CONTENT_MAX_COUNT = 0x10000;
constexpr std::size_t CIA_CONTENT_BITS_SIZE = (CIA_CONTENT_MAX_COUNT / 8);
constexpr std::size_t CIA_HEADER_SIZE = 0x2020;
constexpr std::size_t CIA_CERT_SIZE = 0xA00;
constexpr std::size_t CIA_METADATA_SIZE = 0x3AC0;

class HashedFile;

class CIABuilder {
public:
    explicit CIABuilder();
    ~CIABuilder();

    /**
     * Initializes the building of the CIA.
     * @return true on success, false otherwise
     */
    bool Init(const std::string& destination, TitleMetadata tmd, const std::string& certs_db_path,
              std::size_t total_size, const ProgressCallback& callback);

    /**
     * Adds an NCCH content to the CIA.
     * @return true on success, false otherwise
     */
    bool AddContent(u16 content_id, NCCHContainer& ncch);

    /**
     * Finalizes this CIA and write remaining data.
     * @return true on success, false otherwise
     */
    bool Finalize();

    /**
     * Aborts the current work. In fact, only usable during AddContent.
     */
    void Abort();

private:
    struct Header {
        u32_le header_size;
        u16_le type;
        u16_le version;
        u32_le cert_size;
        u32_le tik_size;
        u32_le tmd_size;
        u32_le meta_size;
        u64_le content_size;
        std::array<u8, CIA_CONTENT_BITS_SIZE> content_present;

        bool IsContentPresent(u16 index) const {
            // The content_present is a bit array which defines which content in the TMD
            // is included in the CIA, so check the bit for this index and add if set.
            // The bits in the content index are arranged w/ index 0 as the MSB, 7 as the LSB, etc.
            return (content_present[index >> 3] & (0x80 >> (index & 7)));
        }

        void SetContentPresent(u16 index) {
            content_present[index >> 3] |= (0x80 >> (index & 7));
        }
    };

    static_assert(sizeof(Header) == CIA_HEADER_SIZE, "CIA Header structure size is wrong");

    struct Metadata {
        std::array<u64_le, 0x30> dependencies;
        std::array<u8, 0x180> reserved;
        u32_le core_version;
        std::array<u8, 0xfc> reserved_2;
        std::array<u8, 0x36c0> icon_data;
    };

    static_assert(sizeof(Metadata) == CIA_METADATA_SIZE, "CIA Metadata structure size is wrong");

    bool WriteCert(const std::string& certs_db_path);

    Header header{};
    Metadata meta{};

    TitleMetadata tmd;

    std::size_t cert_offset{};
    std::size_t ticket_offset{};
    std::size_t tmd_offset{};
    std::size_t content_offset{};

    std::shared_ptr<HashedFile> file;
    std::size_t written{}; // size written (with alignment)
    std::size_t total_size{};
    ProgressCallback callback;

    // The NCCH to abort on
    std::mutex abort_ncch_mutex;
    NCCHContainer* abort_ncch{};
};

} // namespace Core
