#include "hakoniwa/pdu/comm/comm_storage.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace fs = std::filesystem;

fs::path StorageComm::resolve_under_base_(const fs::path& base_dir, const std::string& maybe_rel)
{
    fs::path p(maybe_rel);
    if (p.is_absolute()) {
        return p.lexically_normal();
    }
    return (base_dir / p).lexically_normal();
}

void StorageComm::write_le32_(std::ofstream& ofs, std::uint32_t value)
{
    const char header[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu)
    };
    ofs.write(header, sizeof(header));
}

bool StorageComm::read_le32_(std::ifstream& ifs, std::uint32_t& value)
{
    char header[4];
    if (!ifs.read(header, sizeof(header))) {
        return false;
    }
    value = static_cast<std::uint32_t>(static_cast<unsigned char>(header[0]))
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(header[1])) << 8)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(header[2])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(header[3])) << 24);
    return true;
}

std::uint64_t StorageComm::now_ns_() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

HakoPduErrorType StorageComm::raw_open(const std::string& config_path)
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (is_open_) {
        return HAKO_PDU_ERR_BUSY;
    }

    HakoPduErrorType err = parse_config_(config_path);
    if (err != HAKO_PDU_ERR_OK) {
        return err;
    }

    std::error_code ec;
    if (!path_.parent_path().empty()) {
        fs::create_directories(path_.parent_path(), ec);
        if (ec) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    latest_packets_.clear();
    latest_index_.clear();
    queue_offsets_.clear();
    queue_read_offset_ = 0;
    queue_data_offset_ = 0;
    if (mode_ == Mode::Latest) {
        if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN && !fs::exists(path_)) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }
        err = initialize_latest_file_();
        if (err != HAKO_PDU_ERR_OK) {
            return err;
        }
    } else {
        if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN && !fs::exists(path_)) {
            return HAKO_PDU_ERR_FILE_NOT_FOUND;
        }
        if (!fs::exists(path_)) {
            std::fstream fsio(path_, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!fsio.is_open()) {
                return HAKO_PDU_ERR_IO_ERROR;
            }
            queue_data_offset_ = sizeof(StorageHeader);
            HakoPduErrorType header_err = write_storage_header_(fsio);
            if (header_err != HAKO_PDU_ERR_OK) {
                return header_err;
            }
        } else {
            err = validate_queue_file_();
            if (err != HAKO_PDU_ERR_OK) {
                return err;
            }
        }
        queue_read_offset_ = queue_data_offset_;
    }

    is_open_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::raw_close() noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    is_running_ = false;
    is_open_ = false;
    latest_packets_.clear();
    latest_index_.clear();
    queue_offsets_.clear();
    queue_read_offset_ = 0;
    queue_data_offset_ = 0;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::raw_start() noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (!is_open_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    is_running_ = true;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::raw_stop() noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    is_running_ = false;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::raw_is_running(bool& running) noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    running = is_running_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::recv(const PduResolvedKey& pdu_key,
                                   std::span<std::byte> data,
                                   size_t& received_size) noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    received_size = 0;
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    const auto key = LatestKey{pdu_key.robot, pdu_key.channel_id};
    if (mode_ == Mode::Queue) {
        std::ifstream ifs(path_, std::ios::binary);
        if (!ifs.is_open()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        auto& read_offset = queue_offsets_[key];
        if (read_offset == 0) {
            read_offset = queue_data_offset_;
        }
        ifs.seekg(static_cast<std::streamoff>(read_offset), std::ios::beg);
        if (!ifs.good()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        while (true) {
            const auto frame_start = ifs.tellg();
            std::uint32_t packet_size = 0;
            if (!read_le32_(ifs, packet_size)) {
                if (ifs.eof()) {
                    ifs.clear();
                    ifs.seekg(0, std::ios::end);
                    read_offset = static_cast<std::uint64_t>(ifs.tellg());
                    return HAKO_PDU_ERR_NO_ENTRY;
                }
                return HAKO_PDU_ERR_IO_ERROR;
            }

            std::vector<std::byte> packet(packet_size);
            if (!ifs.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()))) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }

            auto decoded = DataPacket::decode(packet, packet_version());
            if (!decoded) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }

            const auto frame_end = ifs.tellg();
            if (decoded->get_robot_name() == pdu_key.robot
                && decoded->get_channel_id() == static_cast<std::uint32_t>(pdu_key.channel_id)) {
                const auto& payload = decoded->get_pdu_data();
                received_size = payload.size();
                if (data.size() < payload.size()) {
                    return HAKO_PDU_ERR_NO_SPACE;
                }
                std::copy(payload.begin(), payload.end(), data.begin());
                read_offset = static_cast<std::uint64_t>(frame_end);
                return HAKO_PDU_ERR_OK;
            }
            if (frame_end <= frame_start) {
                return HAKO_PDU_ERR_INVALID_CONFIG;
            }
        }
    }

    auto it_index = latest_index_.find(key);
    if (it_index == latest_index_.end()) {
        return HAKO_PDU_ERR_INVALID_PDU_KEY;
    }
    if (it_index->second.timestamp_ns == 0) {
        return HAKO_PDU_ERR_NO_ENTRY;
    }

    auto it_packet = latest_packets_.find(key);
    if (it_packet == latest_packets_.end()) {
        return HAKO_PDU_ERR_NO_ENTRY;
    }
    auto decoded = DataPacket::decode(it_packet->second, packet_version());
    if (!decoded) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    const auto& payload = decoded->get_pdu_data();
    received_size = payload.size();
    if (data.size() < payload.size()) {
        return HAKO_PDU_ERR_NO_SPACE;
    }
    std::copy(payload.begin(), payload.end(), data.begin());
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::recv_next(PduRecord& out) noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    out.payload.clear();
    out.timestamp_ns = 0;
    out.key = {};
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (mode_ != Mode::Queue) {
        return HAKO_PDU_ERR_UNSUPPORTED;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_OUT) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    std::ifstream ifs(path_, std::ios::binary);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (queue_read_offset_ == 0) {
        queue_read_offset_ = queue_data_offset_;
    }
    ifs.seekg(static_cast<std::streamoff>(queue_read_offset_), std::ios::beg);
    if (!ifs.good()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    std::uint32_t packet_size = 0;
    if (!read_le32_(ifs, packet_size)) {
        if (ifs.eof()) {
            ifs.clear();
            ifs.seekg(0, std::ios::end);
            queue_read_offset_ = static_cast<std::uint64_t>(ifs.tellg());
            return HAKO_PDU_ERR_NO_ENTRY;
        }
        return HAKO_PDU_ERR_IO_ERROR;
    }

    std::vector<std::byte> packet(packet_size);
    if (!ifs.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()))) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    auto decoded = DataPacket::decode(packet, packet_version());
    if (!decoded) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    out.key.robot = decoded->get_robot_name();
    out.key.channel_id = static_cast<HakoPduChannelIdType>(decoded->get_channel_id());
    out.payload = decoded->get_pdu_data();
    out.timestamp_ns = 0;
    queue_read_offset_ = static_cast<std::uint64_t>(ifs.tellg());
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::raw_send(const std::vector<std::byte>& data) noexcept
{
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (!is_running_) {
        return HAKO_PDU_ERR_NOT_RUNNING;
    }
    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    auto packet = DataPacket::decode(data, packet_version());
    if (!packet) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }

    if (mode_ == Mode::Queue) {
        std::fstream fsio(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!fsio.is_open()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        fsio.seekp(0, std::ios::end);
        const std::uint32_t packet_size = static_cast<std::uint32_t>(data.size());
        const char len_buf[4] = {
            static_cast<char>(packet_size & 0xFFu),
            static_cast<char>((packet_size >> 8) & 0xFFu),
            static_cast<char>((packet_size >> 16) & 0xFFu),
            static_cast<char>((packet_size >> 24) & 0xFFu)
        };
        fsio.write(len_buf, sizeof(len_buf));
        fsio.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!fsio.good()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        return write_storage_header_(fsio);
    }

    if (!pdu_def_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    PduDef def;
    const auto key = LatestKey{packet->get_robot_name(), static_cast<HakoPduChannelIdType>(packet->get_channel_id())};
    if (!pdu_def_->resolve(key.first, key.second, def)) {
        return HAKO_PDU_ERR_INVALID_PDU_KEY;
    }
    if (packet->get_pdu_data().size() != def.pdu_size) {
        return HAKO_PDU_ERR_INVALID_ARGUMENT;
    }
    auto it = latest_index_.find(key);
    if (it == latest_index_.end()) {
        return HAKO_PDU_ERR_INVALID_PDU_KEY;
    }

    std::fstream fsio(path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!fsio.is_open()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    fsio.seekp(static_cast<std::streamoff>(it->second.packet_offset), std::ios::beg);
    fsio.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!fsio.good()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    latest_packets_[key] = data;
    it->second.timestamp_ns = now_ns_();
    return write_latest_index_entry_(fsio, key);
}

HakoPduErrorType StorageComm::parse_config_(const std::string& config_path)
{
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_FILE_NOT_FOUND;
    }

    nlohmann::json config;
    try {
        ifs >> config;
    } catch (const nlohmann::json::exception&) {
        return HAKO_PDU_ERR_INVALID_JSON;
    }

    if (!config.contains("protocol") || config.at("protocol").get<std::string>() != "storage") {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    if (!config.contains("direction") || !config.contains("storage") || !config.at("storage").is_object()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    direction_ = parse_direction(config.at("direction").get<std::string>());

    if (config.contains("comm_raw_version")) {
        if (!config.at("comm_raw_version").is_string()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!set_packet_version(config.at("comm_raw_version").get<std::string>())) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
    }

    const auto& storage = config.at("storage");
    const std::string backend_value = storage.value("backend", "file");
    if (backend_value != "file") {
        return HAKO_PDU_ERR_UNSUPPORTED;
    }
    backend_ = Backend::File;

    const std::string mode_value = storage.value("mode", "queue");
    if (mode_value == "latest") {
        mode_ = Mode::Latest;
    } else if (mode_value == "queue") {
        mode_ = Mode::Queue;
    } else {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    if (!storage.contains("path") || !storage.at("path").is_string()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    path_ = resolve_under_base_(fs::path(config_path).parent_path(), storage.at("path").get<std::string>());
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::validate_queue_file_()
{
    std::ifstream ifs(path_, std::ios::binary);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    StorageHeader header{};
    if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (header.magic != storage::kStorageHeaderMagic
        || header.version != storage::kStorageVersion
        || header.mode != storage::kStorageModeQueue) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    std::error_code ec;
    const auto actual_file_size = fs::file_size(path_, ec);
    if (ec) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (header.file_size != actual_file_size) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    queue_data_offset_ = header.data_offset;
    ifs.seekg(static_cast<std::streamoff>(queue_data_offset_), std::ios::beg);

    while (true) {
        const auto frame_start = ifs.tellg();
        std::uint32_t packet_size = 0;
        if (!read_le32_(ifs, packet_size)) {
            if (ifs.eof()) {
                return HAKO_PDU_ERR_OK;
            }
            return HAKO_PDU_ERR_IO_ERROR;
        }

        std::vector<std::byte> packet(packet_size);
        if (!ifs.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()))) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (!DataPacket::decode(packet, packet_version())) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        if (ifs.tellg() <= frame_start) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
    }
}

HakoPduErrorType StorageComm::initialize_latest_file_()
{
    if (!pdu_def_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    if (!fs::exists(path_)) {
        std::fstream fsio(path_, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!fsio.is_open()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }

        const auto entries = pdu_def_->list_entries();
        StorageHeader header{};
        header.magic = storage::kStorageHeaderMagic;
        header.version = storage::kStorageVersion;
        header.mode = storage::kStorageModeLatest;
        header.packet_version = (packet_version() == "v1") ? 1 : 2;
        header.key_count = entries.size();
        header.index_offset = sizeof(StorageHeader);
        header.data_offset = header.index_offset + (sizeof(StorageEntry) * entries.size());

        std::uint64_t next_offset = header.data_offset;
        for (const auto& entry : entries) {
            const LatestKey key{entry.robot_name, entry.def.channel_id};
            DataPacket packet(entry.robot_name,
                              static_cast<std::uint32_t>(entry.def.channel_id),
                              std::vector<std::byte>(entry.def.pdu_size, std::byte{0}));
            auto encoded = packet.encode(packet_version());

            StorageEntry index{};
            std::memset(index.robot_name, 0, sizeof(index.robot_name));
            std::strncpy(index.robot_name, entry.robot_name.c_str(), sizeof(index.robot_name) - 1);
            index.channel_id = static_cast<std::uint32_t>(entry.def.channel_id);
            index.timestamp_ns = 0;
            index.packet_offset = next_offset;
            index.packet_size = static_cast<std::uint32_t>(encoded.size());
            latest_index_[key] = index;
            latest_packets_[key] = std::move(encoded);
            next_offset += index.packet_size;
        }

        header.index_offset = sizeof(StorageHeader);
        header.data_offset = sizeof(StorageHeader) + (sizeof(StorageEntry) * latest_index_.size());
        header.file_size = next_offset;
        fsio.write(reinterpret_cast<const char*>(&header), sizeof(header));
        for (const auto& [key, index] : latest_index_) {
            (void)key;
            fsio.write(reinterpret_cast<const char*>(&index), sizeof(index));
        }
        for (const auto& [key, packet] : latest_packets_) {
            (void)key;
            fsio.write(reinterpret_cast<const char*>(packet.data()), static_cast<std::streamsize>(packet.size()));
        }
        if (!fsio.good()) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
    }

    return load_latest_file_();
}

HakoPduErrorType StorageComm::load_latest_file_()
{
    std::ifstream ifs(path_, std::ios::binary);
    if (!ifs.is_open()) {
        return HAKO_PDU_ERR_IO_ERROR;
    }

    StorageHeader header{};
    if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (header.magic != storage::kStorageHeaderMagic
        || header.version != storage::kStorageVersion
        || header.mode != storage::kStorageModeLatest) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    std::error_code ec;
    const auto actual_file_size = fs::file_size(path_, ec);
    if (ec) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    if (header.file_size != actual_file_size) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    latest_index_.clear();
    latest_packets_.clear();
    ifs.seekg(static_cast<std::streamoff>(header.index_offset), std::ios::beg);
    for (std::uint64_t i = 0; i < header.key_count; ++i) {
        StorageEntry index{};
        if (!ifs.read(reinterpret_cast<char*>(&index), sizeof(index))) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        const LatestKey key{std::string(index.robot_name), static_cast<HakoPduChannelIdType>(index.channel_id)};
        latest_index_[key] = index;
    }

    if (!pdu_def_) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }
    const auto defs = pdu_def_->list_entries();
    if (defs.size() != latest_index_.size()) {
        return HAKO_PDU_ERR_INVALID_CONFIG;
    }

    for (const auto& entry : defs) {
        const LatestKey key{entry.robot_name, entry.def.channel_id};
        auto it = latest_index_.find(key);
        if (it == latest_index_.end()) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        DataPacket packet(entry.robot_name,
                          static_cast<std::uint32_t>(entry.def.channel_id),
                          std::vector<std::byte>(entry.def.pdu_size, std::byte{0}));
        const auto expected_size = packet.encode(packet_version()).size();
        if (it->second.packet_size != expected_size) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        if (it->second.packet_offset + it->second.packet_size > header.file_size) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }

        std::vector<std::byte> packet_data(it->second.packet_size);
        ifs.seekg(static_cast<std::streamoff>(it->second.packet_offset), std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(packet_data.data()), static_cast<std::streamsize>(packet_data.size()))) {
            return HAKO_PDU_ERR_IO_ERROR;
        }
        auto decoded = DataPacket::decode(packet_data, packet_version());
        if (!decoded) {
            return HAKO_PDU_ERR_INVALID_CONFIG;
        }
        latest_packets_[key] = std::move(packet_data);
    }

    if (direction_ == HAKO_PDU_ENDPOINT_DIRECTION_IN) {
        for (const auto& [key, index] : latest_index_) {
            (void)key;
            if (index.timestamp_ns == 0) {
                return HAKO_PDU_ERR_NO_ENTRY;
            }
        }
    }

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType StorageComm::write_storage_header_(std::fstream& fsio) noexcept
{
    StorageHeader header{};
    header.magic = storage::kStorageHeaderMagic;
    header.version = storage::kStorageVersion;
    header.mode = (mode_ == Mode::Latest) ? storage::kStorageModeLatest : storage::kStorageModeQueue;
    header.packet_version = (packet_version() == "v1") ? 1 : 2;
    header.key_count = (mode_ == Mode::Latest) ? latest_index_.size() : 0;
    header.index_offset = sizeof(StorageHeader);
    header.data_offset = (mode_ == Mode::Latest)
        ? (sizeof(StorageHeader) + (sizeof(StorageEntry) * latest_index_.size()))
        : sizeof(StorageHeader);
    fsio.flush();
    fsio.seekp(0, std::ios::end);
    const auto end_pos = fsio.tellp();
    if (end_pos < 0) {
        return HAKO_PDU_ERR_IO_ERROR;
    }
    header.file_size = static_cast<std::uint64_t>(end_pos);

    fsio.seekp(0, std::ios::beg);
    fsio.write(reinterpret_cast<const char*>(&header), sizeof(header));
    return fsio.good() ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

HakoPduErrorType StorageComm::write_latest_index_entry_(std::fstream& fsio, const LatestKey& key) noexcept
{
    auto it = latest_index_.find(key);
    if (it == latest_index_.end()) {
        return HAKO_PDU_ERR_INVALID_PDU_KEY;
    }
    const auto index_pos = sizeof(StorageHeader)
        + (static_cast<std::streamoff>(std::distance(latest_index_.begin(), it)) * static_cast<std::streamoff>(sizeof(StorageEntry)));
    fsio.seekp(index_pos, std::ios::beg);
    fsio.write(reinterpret_cast<const char*>(&it->second), sizeof(it->second));
    return fsio.good() ? HAKO_PDU_ERR_OK : HAKO_PDU_ERR_IO_ERROR;
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
