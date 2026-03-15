#include "hakoniwa/pdu/comm/packet.hpp"
#include "hakoniwa/pdu/comm/storage_format.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using hakoniwa::pdu::comm::DataPacket;
using hakoniwa::pdu::comm::MetaRequestType;
using hakoniwa::pdu::comm::storage::StorageEntry;
using hakoniwa::pdu::comm::storage::StorageHeader;

namespace {

struct Options {
    fs::path path;
    std::optional<std::size_t> limit;
    bool json{false};
    bool verbose{false};
};

struct DecodedPacketSummary {
    std::string robot_name;
    std::uint32_t channel_id{0};
    std::size_t payload_size{0};
    std::string request_type;
    std::int64_t hako_time_us{0};
    std::int64_t asset_time_us{0};
    std::int64_t real_time_us{0};
};

bool read_le32(std::ifstream& ifs, std::uint32_t& value)
{
    char buf[4];
    if (!ifs.read(buf, sizeof(buf))) {
        return false;
    }
    value = static_cast<std::uint32_t>(static_cast<unsigned char>(buf[0]))
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[1])) << 8)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[2])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(buf[3])) << 24);
    return true;
}

std::string packet_version_string(std::uint16_t packet_version)
{
    return (packet_version == 1) ? "v1" : "v2";
}

std::string storage_mode_string(std::uint16_t mode)
{
    switch (mode) {
    case hakoniwa::pdu::comm::storage::kStorageModeQueue:
        return "queue";
    case hakoniwa::pdu::comm::storage::kStorageModeLatest:
        return "latest";
    default:
        return "unknown";
    }
}

std::string request_type_string(std::uint32_t request_type)
{
    switch (request_type) {
    case static_cast<std::uint32_t>(MetaRequestType::PDU_DATA_TYPE):
        return "pdu_data";
    case static_cast<std::uint32_t>(MetaRequestType::DECLARE_PDU_FOR_READ):
        return "declare_read";
    case static_cast<std::uint32_t>(MetaRequestType::DECLARE_PDU_FOR_WRITE):
        return "declare_write";
    case static_cast<std::uint32_t>(MetaRequestType::REQUEST_PDU_READ):
        return "request_read";
    case static_cast<std::uint32_t>(MetaRequestType::REGISTER_RPC_CLIENT):
        return "register_rpc";
    case static_cast<std::uint32_t>(MetaRequestType::PDU_DATA_RPC_REQUEST):
        return "rpc_request";
    case static_cast<std::uint32_t>(MetaRequestType::PDU_DATA_RPC_REPLY):
        return "rpc_reply";
    default:
        return "unknown";
    }
}

bool read_header(std::ifstream& ifs, StorageHeader& header)
{
    if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return false;
    }
    return true;
}

std::optional<DecodedPacketSummary> decode_packet_summary(const std::vector<std::byte>& packet_bytes,
                                                          const std::string& packet_version)
{
    auto packet = DataPacket::decode(packet_bytes, packet_version);
    if (!packet) {
        return std::nullopt;
    }
    DecodedPacketSummary summary;
    summary.robot_name = packet->get_robot_name();
    summary.channel_id = packet->get_channel_id();
    summary.payload_size = packet->get_pdu_data().size();
    if (packet_version == "v2") {
        const auto& meta = packet->get_meta();
        summary.request_type = request_type_string(meta.meta_request_type);
        summary.hako_time_us = meta.hako_time_us;
        summary.asset_time_us = meta.asset_time_us;
        summary.real_time_us = meta.real_time_us;
    } else {
        summary.request_type = packet->is_pdu_data_type(packet_version) ? "pdu_data" : "control_or_unknown";
    }
    return summary;
}

void validate_common_header(const StorageHeader& header, std::uint64_t actual_file_size)
{
    if (header.magic != hakoniwa::pdu::comm::storage::kStorageHeaderMagic) {
        throw std::runtime_error("Invalid storage header magic");
    }
    if (header.version != hakoniwa::pdu::comm::storage::kStorageVersion) {
        throw std::runtime_error("Unsupported storage version");
    }
    if (header.file_size != actual_file_size) {
        throw std::runtime_error("Storage file size mismatch");
    }
    if (header.index_offset > header.file_size || header.data_offset > header.file_size) {
        throw std::runtime_error("Storage header offset is out of range");
    }
    if (header.mode != hakoniwa::pdu::comm::storage::kStorageModeQueue
        && header.mode != hakoniwa::pdu::comm::storage::kStorageModeLatest) {
        throw std::runtime_error("Unsupported storage mode");
    }
}

void validate_latest_header(const StorageHeader& header)
{
    const auto max_entries = (header.file_size >= header.index_offset)
        ? ((header.file_size - header.index_offset) / sizeof(StorageEntry))
        : 0;
    if (header.key_count > max_entries) {
        throw std::runtime_error("Latest key_count exceeds file bounds");
    }
    const auto index_end = header.index_offset + (header.key_count * sizeof(StorageEntry));
    if (index_end > header.file_size || index_end > header.data_offset) {
        throw std::runtime_error("Latest index area is out of range");
    }
}

void validate_queue_header(const StorageHeader& header)
{
    if (header.key_count != 0) {
        throw std::runtime_error("Queue storage must not have key_count");
    }
    if (header.data_offset < sizeof(StorageHeader)) {
        throw std::runtime_error("Queue data_offset is invalid");
    }
}

int print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0 << " <storage-file> [--limit N] [--json] [--verbose]\n";
    return 1;
}

std::optional<Options> parse_args(int argc, char** argv)
{
    if (argc < 2) {
        return std::nullopt;
    }
    Options options;
    options.path = argv[1];
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg == "--limit") {
            if (i + 1 >= argc) {
                return std::nullopt;
            }
            options.limit = static_cast<std::size_t>(std::stoull(argv[++i]));
            continue;
        }
        return std::nullopt;
    }
    return options;
}

nlohmann::json make_common_json(const StorageHeader& header,
                                const fs::path& path,
                                std::uint64_t actual_file_size)
{
    return nlohmann::json{
        {"path", fs::absolute(path).lexically_normal().string()},
        {"mode", storage_mode_string(header.mode)},
        {"packet_version", packet_version_string(header.packet_version)},
        {"file_size", header.file_size},
        {"actual_file_size", actual_file_size},
        {"key_count", header.key_count},
        {"index_offset", header.index_offset},
        {"data_offset", header.data_offset}
    };
}

nlohmann::json make_packet_json(const DecodedPacketSummary& summary, const std::string& packet_version)
{
    nlohmann::json packet_json{
        {"key", {
            {"robot_name", summary.robot_name},
            {"channel_id", summary.channel_id}
        }},
        {"payload_size", summary.payload_size},
        {"request_type", summary.request_type}
    };
    if (packet_version == "v2") {
        packet_json["packet_timestamps"] = {
            {"hako_time_us", summary.hako_time_us},
            {"asset_time_us", summary.asset_time_us},
            {"real_time_us", summary.real_time_us}
        };
    }
    return packet_json;
}

int print_queue_text(std::ifstream& ifs, const StorageHeader& header, const Options& options)
{
    const auto packet_version = packet_version_string(header.packet_version);
    std::cout << "records:\n";
    ifs.seekg(static_cast<std::streamoff>(header.data_offset), std::ios::beg);
    std::size_t index = 0;
    while (true) {
        if (options.limit && index >= *options.limit) {
            break;
        }
        const auto frame_offset = static_cast<std::uint64_t>(ifs.tellg());
        std::uint32_t packet_size = 0;
        if (!read_le32(ifs, packet_size)) {
            if (ifs.eof()) {
                break;
            }
            std::cerr << "Failed to read queue record size at offset " << frame_offset << "\n";
            return 1;
        }
        if (frame_offset + 4ULL + packet_size > header.file_size) {
            std::cerr << "Queue record exceeds file bounds at offset " << frame_offset << "\n";
            return 1;
        }
        std::vector<std::byte> packet_bytes(packet_size);
        if (!ifs.read(reinterpret_cast<char*>(packet_bytes.data()), static_cast<std::streamsize>(packet_bytes.size()))) {
            std::cerr << "Failed to read queue record payload at offset " << frame_offset << "\n";
            return 1;
        }
        const auto summary = decode_packet_summary(packet_bytes, packet_version);
        if (!summary) {
            std::cerr << "Failed to decode queue packet at offset " << frame_offset << "\n";
            return 1;
        }
        std::cout << "  [" << index << "]"
                  << " frame_offset=" << frame_offset
                  << " packet_size=" << packet_size
                  << " key=" << summary->robot_name << "/" << summary->channel_id
                  << " payload_size=" << summary->payload_size
                  << " request_type=" << summary->request_type;
        if (packet_version == "v2") {
            std::cout << " hako_time_us=" << summary->hako_time_us
                      << " asset_time_us=" << summary->asset_time_us
                      << " real_time_us=" << summary->real_time_us;
        }
        std::cout << "\n";
        ++index;
    }
    return 0;
}

int print_latest_text(std::ifstream& ifs, const StorageHeader& header, const Options& options)
{
    const auto packet_version = packet_version_string(header.packet_version);
    ifs.seekg(static_cast<std::streamoff>(header.index_offset), std::ios::beg);
    std::vector<StorageEntry> entries(header.key_count);
    if (!entries.empty() && !ifs.read(reinterpret_cast<char*>(entries.data()),
                                      static_cast<std::streamsize>(entries.size() * sizeof(StorageEntry)))) {
        std::cerr << "Failed to read latest index\n";
        return 1;
    }

    std::cout << "entries:\n";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (options.limit && i >= *options.limit) {
            break;
        }
        const auto& entry = entries[i];
        const bool initialized = entry.timestamp_ns != 0;
        std::cout << "  [" << i << "]"
                  << " key=" << entry.robot_name << "/" << entry.channel_id
                  << " storage_timestamp_ns=" << entry.timestamp_ns
                  << " packet_offset=" << entry.packet_offset
                  << " packet_size=" << entry.packet_size
                  << " initialized=" << (initialized ? "yes" : "no");
        if (!initialized) {
            std::cout << "\n";
            continue;
        }
        std::vector<std::byte> packet_bytes(entry.packet_size);
        ifs.seekg(static_cast<std::streamoff>(entry.packet_offset), std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(packet_bytes.data()), static_cast<std::streamsize>(packet_bytes.size()))) {
            std::cerr << "\nFailed to read latest packet at offset " << entry.packet_offset << "\n";
            return 1;
        }
        const auto summary = decode_packet_summary(packet_bytes, packet_version);
        if (!summary) {
            std::cerr << "\nFailed to decode latest packet at offset " << entry.packet_offset << "\n";
            return 1;
        }
        std::cout << " payload_size=" << summary->payload_size
                  << " request_type=" << summary->request_type;
        if (packet_version == "v2") {
            std::cout << " hako_time_us=" << summary->hako_time_us
                      << " asset_time_us=" << summary->asset_time_us
                      << " real_time_us=" << summary->real_time_us;
        }
        std::cout << "\n";
    }
    return 0;
}

nlohmann::json build_queue_json(std::ifstream& ifs, const StorageHeader& header, const Options& options)
{
    const auto packet_version = packet_version_string(header.packet_version);
    nlohmann::json records = nlohmann::json::array();
    ifs.seekg(static_cast<std::streamoff>(header.data_offset), std::ios::beg);
    std::size_t index = 0;
    while (true) {
        if (options.limit && index >= *options.limit) {
            break;
        }
        const auto frame_offset = static_cast<std::uint64_t>(ifs.tellg());
        std::uint32_t packet_size = 0;
        if (!read_le32(ifs, packet_size)) {
            if (ifs.eof()) {
                break;
            }
            throw std::runtime_error("Failed to read queue record size");
        }
        if (frame_offset + 4ULL + packet_size > header.file_size) {
            throw std::runtime_error("Queue record exceeds file bounds");
        }
        std::vector<std::byte> packet_bytes(packet_size);
        if (!ifs.read(reinterpret_cast<char*>(packet_bytes.data()), static_cast<std::streamsize>(packet_bytes.size()))) {
            throw std::runtime_error("Failed to read queue record payload");
        }
        const auto summary = decode_packet_summary(packet_bytes, packet_version);
        if (!summary) {
            throw std::runtime_error("Failed to decode queue packet");
        }
        nlohmann::json record = {
            {"index", index},
            {"frame_offset", frame_offset},
            {"packet_offset", frame_offset + 4},
            {"packet_size", packet_size},
            {"storage_timestamp_ns", nullptr},
            {"initialized", true}
        };
        record.update(make_packet_json(*summary, packet_version));
        records.push_back(std::move(record));
        ++index;
    }
    return records;
}

nlohmann::json build_latest_json(std::ifstream& ifs, const StorageHeader& header, const Options& options)
{
    const auto packet_version = packet_version_string(header.packet_version);
    ifs.seekg(static_cast<std::streamoff>(header.index_offset), std::ios::beg);
    std::vector<StorageEntry> entries(header.key_count);
    if (!entries.empty() && !ifs.read(reinterpret_cast<char*>(entries.data()),
                                      static_cast<std::streamsize>(entries.size() * sizeof(StorageEntry)))) {
        throw std::runtime_error("Failed to read latest index");
    }

    nlohmann::json records = nlohmann::json::array();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (options.limit && i >= *options.limit) {
            break;
        }
        const auto& entry = entries[i];
        const bool initialized = entry.timestamp_ns != 0;
        if (entry.packet_offset + entry.packet_size > header.file_size) {
            throw std::runtime_error("Latest packet range is out of file bounds");
        }
        nlohmann::json record = {
            {"index", i},
            {"packet_offset", entry.packet_offset},
            {"packet_size", entry.packet_size},
            {"storage_timestamp_ns", entry.timestamp_ns},
            {"initialized", initialized},
            {"key", {
                {"robot_name", std::string(entry.robot_name)},
                {"channel_id", entry.channel_id}
            }}
        };
        if (initialized) {
            std::vector<std::byte> packet_bytes(entry.packet_size);
            ifs.seekg(static_cast<std::streamoff>(entry.packet_offset), std::ios::beg);
            if (!ifs.read(reinterpret_cast<char*>(packet_bytes.data()), static_cast<std::streamsize>(packet_bytes.size()))) {
                throw std::runtime_error("Failed to read latest packet");
            }
            const auto summary = decode_packet_summary(packet_bytes, packet_version);
            if (!summary) {
                throw std::runtime_error("Failed to decode latest packet");
            }
            record.update(make_packet_json(*summary, packet_version));
        } else {
            record["payload_size"] = nullptr;
            record["request_type"] = nullptr;
            if (packet_version == "v2") {
                record["packet_timestamps"] = nullptr;
            }
        }
        records.push_back(std::move(record));
    }
    return records;
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parse_args(argc, argv);
    if (!options) {
        return print_usage(argv[0]);
    }

    std::ifstream ifs(options->path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open storage file: " << options->path << "\n";
        return 1;
    }

    StorageHeader header{};
    if (!read_header(ifs, header)) {
        std::cerr << "Failed to read storage header\n";
        return 1;
    }

    std::error_code ec;
    const auto actual_file_size = fs::file_size(options->path, ec);
    if (ec) {
        std::cerr << "Failed to stat storage file: " << options->path << "\n";
        return 1;
    }

    try {
        validate_common_header(header, actual_file_size);
        if (header.mode == hakoniwa::pdu::comm::storage::kStorageModeLatest) {
            validate_latest_header(header);
        } else {
            validate_queue_header(header);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    if (options->json) {
        try {
            auto root = make_common_json(header, options->path, actual_file_size);
            if (options->verbose) {
                root["flags"] = header.flags;
            }
            switch (header.mode) {
            case hakoniwa::pdu::comm::storage::kStorageModeLatest:
                root["records"] = build_latest_json(ifs, header, *options);
                break;
            case hakoniwa::pdu::comm::storage::kStorageModeQueue:
                root["records"] = build_queue_json(ifs, header, *options);
                break;
            default:
                throw std::runtime_error("Unsupported storage mode");
            }
            std::cout << root.dump(2) << "\n";
            return 0;
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "path: " << fs::absolute(options->path).lexically_normal().string() << "\n"
              << "mode: " << storage_mode_string(header.mode) << "\n"
              << "packet_version: " << packet_version_string(header.packet_version) << "\n"
              << "file_size: " << header.file_size << "\n"
              << "actual_file_size: " << actual_file_size << "\n"
              << "key_count: " << header.key_count << "\n"
              << "index_offset: " << header.index_offset << "\n"
              << "data_offset: " << header.data_offset << "\n";
    if (options->verbose) {
        std::cout << "flags: " << header.flags << "\n";
    }

    switch (header.mode) {
    case hakoniwa::pdu::comm::storage::kStorageModeLatest:
        return print_latest_text(ifs, header, *options);
    case hakoniwa::pdu::comm::storage::kStorageModeQueue:
        return print_queue_text(ifs, header, *options);
    default:
        std::cerr << "Unsupported storage mode\n";
        return 1;
    }
}
