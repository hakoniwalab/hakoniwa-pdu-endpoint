#pragma once

#include <cstdint>

namespace hakoniwa {
namespace pdu {
namespace comm {
namespace storage {

constexpr std::uint32_t kStorageHeaderMagic = 0x53445048; // HPDS
constexpr std::uint16_t kStorageVersion = 1;
constexpr std::uint16_t kStorageModeQueue = 1;
constexpr std::uint16_t kStorageModeLatest = 2;

struct StorageHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t mode;
    std::uint16_t packet_version;
    std::uint16_t flags;
    std::uint16_t reserved0;
    std::uint32_t reserved1;
    std::uint64_t key_count;
    std::uint64_t index_offset;
    std::uint64_t data_offset;
    std::uint64_t file_size;
};

struct StorageEntry {
    char robot_name[128];
    std::uint32_t channel_id;
    std::uint32_t reserved0;
    std::uint64_t timestamp_ns;
    std::uint64_t packet_offset;
    std::uint32_t packet_size;
    std::uint32_t reserved1;
};

} // namespace storage
} // namespace comm
} // namespace pdu
} // namespace hakoniwa
