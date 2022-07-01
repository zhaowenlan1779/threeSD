// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include "common/assert.h"
#include "common/common_funcs.h"
#include "core/file_sys/data/data_container.h"

namespace Core {

DPFSContainer::DPFSContainer(DPFSDescriptor descriptor_, u8 level1_selector_,
                             std::vector<u32_le> data_)
    : descriptor(std::move(descriptor_)), level1_selector(level1_selector_),
      data(std::move(data_)) {

    ASSERT_MSG(descriptor.magic == MakeMagic('D', 'P', 'F', 'S'), "DPFS Magic is not correct");
    ASSERT_MSG(descriptor.version == 0x10000, "DPFS Version is not correct");
}

bool DPFSContainer::GetBit(u8& out, u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");

    const auto word =
        (descriptor.levels[level].offset + selector * descriptor.levels[level].size) / 4 +
        index / 32;
    if (data.size() <= word) {
        LOG_ERROR(Core, "Out of bound: level {} selector {} index {}", level, selector, index);
        return false;
    }

    out = (data[word] >> (31 - (index % 32))) & static_cast<u32_le>(1);
    return true;
}

bool DPFSContainer::GetByte(u8& out, u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");

    const auto byte =
        descriptor.levels[level].offset + selector * descriptor.levels[level].size + index;
    if (data.size() * 4 <= byte) {
        LOG_ERROR(Core, "Out of bound: level {} selector {} index {}", level, selector, index);
        return false;
    }

    out =
        reinterpret_cast<const u8*>(data.data())[descriptor.levels[level].offset +
                                                 selector * descriptor.levels[level].size + index];
    return true;
}

bool DPFSContainer::GetLevel3Data(std::vector<u8>& out) const {
    std::vector<u8> level3_data(descriptor.levels[2].size);
    for (std::size_t i = 0; i < level3_data.size(); i++) {
        const u64 level2_bit_index = static_cast<u64>(i) >> descriptor.levels[2].block_size;
        const u64 level1_bit_index = (level2_bit_index / 8) >> descriptor.levels[1].block_size;

        u8 level2_selector, level3_selector;
        if (!GetBit(level2_selector, 0, level1_selector, level1_bit_index) ||
            !GetBit(level3_selector, 1, level2_selector, level2_bit_index) ||
            !GetByte(level3_data[i], 2, level3_selector, i)) {
            return false;
        }
    }
    out = std::move(level3_data);
    return true;
}

DataContainer::DataContainer(std::vector<u8> data_) : data(std::move(data_)) {
    if (data.size() < 0x200) {
        LOG_ERROR(Core, "Data size {:X} is too small", data.size());
        is_good = false;
        return;
    }

    u32_le magic;
    std::memcpy(&magic, data.data() + 0x100, sizeof(u32_le));
    if (magic == MakeMagic('D', 'I', 'S', 'A')) {
        is_good = InitAsDISA();
    } else if (magic == MakeMagic('D', 'I', 'F', 'F')) {
        is_good = InitAsDIFF();
    } else {
        LOG_ERROR(Core, "Unknown magic 0x{:08x}", magic);
        is_good = false;
    }
}

DataContainer::~DataContainer() = default;

bool DataContainer::IsGood() const {
    return is_good;
}

bool DataContainer::InitAsDISA() {
    DISAHeader header;
    TRY_MEMCPY(&header, data, 0x100, sizeof(header));

    if (header.version != 0x40000) {
        LOG_ERROR(Core, "DISA Version {:x} is not correct", header.version);
        return false;
    }

    if (header.active_partition_table == 0) { // primary
        partition_table_offset = header.primary_partition_table_offset;
    } else {
        partition_table_offset = header.secondary_partition_table_offset;
    }

    partition_count = header.partition_count;

    if (header.partition_count == 2) {
        partition_descriptors = {header.partition_descriptors[0], header.partition_descriptors[1]};
        partitions = {header.partitions[0], header.partitions[1]};
    } else {
        partition_descriptors = {header.partition_descriptors[0]};
        partitions = {header.partitions[0]};
    }

    return true;
}

bool DataContainer::InitAsDIFF() {
    DIFFHeader header;
    TRY_MEMCPY(&header, data, 0x100, sizeof(header));

    if (header.version != 0x30000) {
        LOG_ERROR(Core, "DIFF Version {:x} is not correct", header.version);
        return false;
    }

    if (header.active_partition_table == 0) { // primary
        partition_table_offset = header.primary_partition_table_offset;
    } else {
        partition_table_offset = header.secondary_partition_table_offset;
    }

    partition_count = 1;
    partition_descriptors = {{/* offset */ 0, /* size */ header.partition_table_size}};
    partitions = {header.partition_A};

    return true;
}

bool DataContainer::GetPartitionData(std::vector<u8>& out, u8 index) const {
    auto partition_descriptor_offset = partition_table_offset + partition_descriptors[index].offset;

    DIFIHeader difi;
    TRY_MEMCPY(&difi, data, partition_descriptor_offset, sizeof(difi));

    if (difi.magic != MakeMagic('D', 'I', 'F', 'I') || difi.version != 0x10000) {
        LOG_ERROR(Core, "Invalid magic {:08x} or version {}", difi.magic, difi.version);
        return false;
    }

    ASSERT_MSG(difi.ivfc.size >= sizeof(IVFCDescriptor), "IVFC descriptor size is too small");
    IVFCDescriptor ivfc_descriptor;
    TRY_MEMCPY(&ivfc_descriptor, data, partition_descriptor_offset + difi.ivfc.offset,
               sizeof(ivfc_descriptor));

    if (difi.enable_external_IVFC_level_4) {
        if (data.size() < partitions[index].offset + difi.external_IVFC_level_4_offset +
                              ivfc_descriptor.levels[3].size) {
            LOG_ERROR(Core, "File size is too small");
            return false;
        }
        out = std::vector<u8>(
            data.data() + partitions[index].offset + difi.external_IVFC_level_4_offset,
            data.data() + partitions[index].offset + difi.external_IVFC_level_4_offset +
                ivfc_descriptor.levels[3].size);
        return true;
    }

    // Unwrap DPFS Tree
    ASSERT_MSG(difi.dpfs.size >= sizeof(DPFSDescriptor), "DPFS descriptor size is too small");
    DPFSDescriptor dpfs_descriptor;
    TRY_MEMCPY(&dpfs_descriptor, data, partition_descriptor_offset + difi.dpfs.offset,
               sizeof(dpfs_descriptor));

    std::vector<u32_le> partition_data(partitions[index].size / 4);
    TRY_MEMCPY(partition_data.data(), data, partitions[index].offset, partitions[index].size);

    DPFSContainer dpfs_container(std::move(dpfs_descriptor), difi.dpfs_level1_selector,
                                 std::move(partition_data));

    std::vector<u8> ivfc_data;
    if (!dpfs_container.GetLevel3Data(ivfc_data)) {
        return false;
    }

    if (ivfc_data.size() < ivfc_descriptor.levels[3].offset + ivfc_descriptor.levels[3].size) {
        LOG_ERROR(Core, "IVFC data size is too small");
        return false;
    }

    out = std::vector<u8>(ivfc_data.data() + ivfc_descriptor.levels[3].offset,
                          ivfc_data.data() + ivfc_descriptor.levels[3].offset +
                              ivfc_descriptor.levels[3].size);
    return true;
}

bool DataContainer::GetIVFCLevel4Data(std::vector<std::vector<u8>>& out) const {
    if (partition_count == 1) {
        out.resize(1);
        return GetPartitionData(out[0], 0);
    } else {
        out.resize(2);
        return GetPartitionData(out[0], 0) && GetPartitionData(out[1], 1);
    }
}

} // namespace Core
