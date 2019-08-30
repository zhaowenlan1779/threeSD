// Copyright 2019 threeSD Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cmath>
#include "common/assert.h"
#include "core/data_container.h"

namespace Core {

constexpr u32 MakeMagic(char a, char b, char c, char d) {
    return a | b << 8 | c << 16 | d << 24;
}

DPFSContainer::DPFSContainer(DPFSDescriptor descriptor_, u8 level1_selector_,
                             std::vector<u32_le> data_)
    : descriptor(descriptor_), level1_selector(level1_selector_), data(std::move(data_)) {

    ASSERT_MSG(descriptor.magic == MakeMagic('D', 'P', 'F', 'S'), "DPFS Magic is not correct");
    ASSERT_MSG(descriptor.version == 0x10000, "DPFS Version is not correct");
}

u8 DPFSContainer::GetBit(u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");
    return (data[(descriptor.levels[level].offset + selector * descriptor.levels[level].size) / 4 +
                 index / 32] >>
            (31 - (index % 32))) &
           static_cast<u32_le>(1);
}

u8 DPFSContainer::GetByte(u8 level, u8 selector, u64 index) const {
    ASSERT_MSG(level <= 2 && selector <= 1, "Level or selector invalid");
    return reinterpret_cast<const u8*>(
        data.data())[descriptor.levels[level].offset + selector * descriptor.levels[level].size +
                     index];
}

std::vector<u8> DPFSContainer::GetLevel3Data() const {
    std::vector<u8> level3_data(descriptor.levels[2].size);
    for (std::size_t i = 0; i < level3_data.size(); i++) {
        auto level2_bit_index = i / std::pow(2, descriptor.levels[2].block_size);
        auto level1_bit_index =
            (level2_bit_index / 8) / std::pow(2, descriptor.levels[1].block_size);
        auto level2_selector = GetBit(0, level1_selector, level1_bit_index);
        auto level3_selector = GetBit(1, level2_selector, level2_bit_index);
        level3_data[i] = GetByte(2, level3_selector, i);
    }
    return level3_data;
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
    std::memcpy(&header, data.data() + 0x100, sizeof(header));

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
    std::memcpy(&header, data.data() + 0x100, sizeof(header));

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

std::vector<u8> DataContainer::GetPartitionData(u8 index) const {
    auto partition_descriptor_offset = partition_table_offset + partition_descriptors[index].offset;

    DIFIHeader difi;
    std::memcpy(&difi, data.data() + partition_descriptor_offset, sizeof(difi));
    ASSERT_MSG(difi.magic == MakeMagic('D', 'I', 'F', 'I'), "DIFI Magic is not correct");
    ASSERT_MSG(difi.version == 0x10000, "DIFI Version is not correct");

    ASSERT_MSG(difi.ivfc.size >= sizeof(IVFCDescriptor), "IVFC descriptor size is too small");
    IVFCDescriptor ivfc_descriptor;
    std::memcpy(&ivfc_descriptor, data.data() + partition_descriptor_offset + difi.ivfc.offset,
                sizeof(ivfc_descriptor));

    if (difi.enable_external_IVFC_level_4) {
        std::vector<u8> result(
            data.data() + partitions[index].offset + difi.external_IVFC_level_4_offset,
            data.data() + partitions[index].offset + difi.external_IVFC_level_4_offset +
                ivfc_descriptor.levels[3].size);
        return result;
    }

    // Unwrap DPFS Tree
    ASSERT_MSG(difi.dpfs.size >= sizeof(DPFSDescriptor), "DPFS descriptor size is too small");
    DPFSDescriptor dpfs_descriptor;
    std::memcpy(&dpfs_descriptor, data.data() + partition_descriptor_offset + difi.dpfs.offset,
                sizeof(dpfs_descriptor));

    std::vector<u32_le> partition_data(partitions[index].size / 4);
    std::memcpy(partition_data.data(), data.data() + partitions[index].offset,
                partitions[index].size);

    DPFSContainer dpfs_container(dpfs_descriptor, difi.dpfs_level1_selector,
                                 std::move(partition_data));
    auto ivfc_data = dpfs_container.GetLevel3Data();

    std::vector<u8> result(ivfc_data.data() + ivfc_descriptor.levels[3].offset,
                           ivfc_data.data() + ivfc_descriptor.levels[3].offset +
                               ivfc_descriptor.levels[3].size);
    return result;
}

std::vector<std::vector<u8>> DataContainer::GetIVFCLevel4Data() const {
    if (partition_count == 1) {
        return {GetPartitionData(0)};
    } else {
        return {GetPartitionData(0), GetPartitionData(1)};
    }
}

} // namespace Core
