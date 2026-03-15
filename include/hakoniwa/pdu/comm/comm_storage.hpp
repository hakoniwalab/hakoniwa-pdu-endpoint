#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include "hakoniwa/pdu/pdu_definition.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

// Storage comm persists raw framed packets so they can be replayed or inspected later.
// The on-disk record format is:
//   [4-byte little-endian packet_size][packet bytes]
class StorageComm final : public PduCommRaw
{
public:
    StorageComm() = default;
    ~StorageComm() override = default;
    HakoPduErrorType recv(const PduResolvedKey& pdu_key, std::span<std::byte> data, size_t& received_size) noexcept override;

protected:
    HakoPduErrorType raw_open(const std::string& config_path) override;
    HakoPduErrorType raw_close() noexcept override;
    HakoPduErrorType raw_start() noexcept override;
    HakoPduErrorType raw_stop() noexcept override;
    HakoPduErrorType raw_is_running(bool& running) noexcept override;
    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override;

private:
    enum class Backend {
        File
    };
    enum class Mode {
        Latest,
        Queue
    };

    using LatestKey = std::pair<std::string, HakoPduChannelIdType>;

    struct LatestHeader {
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

    struct LatestIndexEntry {
        char robot_name[128];
        std::uint32_t channel_id;
        std::uint32_t reserved0;
        std::uint64_t timestamp_ns;
        std::uint64_t packet_offset;
        std::uint32_t packet_size;
        std::uint32_t reserved1;
    };

    static std::filesystem::path resolve_under_base_(const std::filesystem::path& base_dir, const std::string& maybe_rel);
    static void write_le32_(std::ofstream& ofs, std::uint32_t value);
    static bool read_le32_(std::ifstream& ifs, std::uint32_t& value);
    static std::uint64_t now_ns_() noexcept;

    HakoPduErrorType parse_config_(const std::string& config_path);
    HakoPduErrorType initialize_latest_file_();
    HakoPduErrorType validate_queue_file_();
    HakoPduErrorType load_latest_file_();
    HakoPduErrorType write_latest_header_(std::fstream& fs) noexcept;
    HakoPduErrorType write_latest_index_entry_(std::fstream& fs, const LatestKey& key) noexcept;

    Backend backend_{Backend::File};
    Mode mode_{Mode::Queue};
    HakoPduEndpointDirectionType direction_{HAKO_PDU_ENDPOINT_DIRECTION_OUT};
    std::filesystem::path path_;

    bool is_open_{false};
    bool is_running_{false};
    std::mutex io_mutex_;
    std::map<LatestKey, std::vector<std::byte>> latest_packets_;
    std::map<LatestKey, LatestIndexEntry> latest_index_;
    std::map<LatestKey, std::uint64_t> queue_offsets_;
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
