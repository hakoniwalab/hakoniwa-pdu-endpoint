#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include "hakoniwa/pdu/comm/storage_format.hpp"
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
    HakoPduErrorType recv_next(PduRecord& out) noexcept override;

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
    using StorageHeader = storage::StorageHeader;
    using StorageEntry = storage::StorageEntry;

    static std::filesystem::path resolve_under_base_(const std::filesystem::path& base_dir, const std::string& maybe_rel);
    static void write_le32_(std::ofstream& ofs, std::uint32_t value);
    static bool read_le32_(std::ifstream& ifs, std::uint32_t& value);
    static std::uint64_t now_ns_() noexcept;

    HakoPduErrorType parse_config_(const std::string& config_path);
    HakoPduErrorType initialize_latest_file_();
    HakoPduErrorType validate_queue_file_();
    HakoPduErrorType load_latest_file_();
    HakoPduErrorType write_storage_header_(std::fstream& fs) noexcept;
    HakoPduErrorType write_latest_index_entry_(std::fstream& fs, const LatestKey& key) noexcept;

    Backend backend_{Backend::File};
    Mode mode_{Mode::Queue};
    HakoPduEndpointDirectionType direction_{HAKO_PDU_ENDPOINT_DIRECTION_OUT};
    std::filesystem::path path_;

    bool is_open_{false};
    bool is_running_{false};
    std::mutex io_mutex_;
    std::map<LatestKey, std::vector<std::byte>> latest_packets_;
    std::map<LatestKey, StorageEntry> latest_index_;
    std::map<LatestKey, std::uint64_t> queue_offsets_;
    std::uint64_t queue_read_offset_{0};
    std::uint64_t queue_data_offset_{0};
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
