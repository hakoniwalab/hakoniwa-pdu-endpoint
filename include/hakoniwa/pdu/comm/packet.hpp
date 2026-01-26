#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h> // For htonl, ntohl

namespace hakoniwa {
namespace pdu {
namespace comm {

// Constants from the Python implementation
constexpr uint32_t HAKO_META_MAGIC = 0x48414B4F; // "HAKO"
constexpr uint16_t HAKO_META_VER_V1 = 0x0001;
constexpr uint16_t HAKO_META_VER_V2 = 0x0002;
constexpr size_t META_V2_FIXED_SIZE = 176;
// This seems to be based on an external definition. Let's assume 128 for now.
//TOTAL_PDU_META_SIZE = PduMetaData.PDU_META_DATA_SIZE + META_FIXED_SIZE
// 304 = 128 + 176
constexpr size_t PDU_META_DATA_SIZE = 128;
constexpr size_t TOTAL_PDU_META_SIZE = PDU_META_DATA_SIZE + META_V2_FIXED_SIZE; // 304 bytes

enum MetaRequestType : uint32_t {
    PDU_DATA_TYPE = 0x42555043,   // "CPUB"
    DECLARE_PDU_FOR_READ = 0x52455044,   // "REPD"
    DECLARE_PDU_FOR_WRITE = 0x57505044,  // "WPPD"
    REQUEST_PDU_READ = 0x57505045, // "RPDR" (adjusted from python's WPPE)
    REGISTER_RPC_CLIENT    = 0x43505244,   // "DRPC"
    PDU_DATA_RPC_REQUEST     = 0x43505243,   // "CRPC"
    PDU_DATA_RPC_REPLY       = 0x43505253,   // "SRPC"
};


#pragma pack(push, 1)
struct MetaPdu {
    // PduMetaData part (assumed 128 bytes)
    char               robot_name[128];

    // MetaV2 fixed part (176 bytes)
    uint32_t           magicno;
    uint16_t           version;
    uint16_t           reserved; // for alignment
    uint32_t           flags;
    uint32_t           meta_request_type;
    uint32_t           total_len;
    uint32_t           body_len;
    int64_t            hako_time_us;
    int64_t            asset_time_us;
    int64_t            real_time_us;
    uint32_t           channel_id;
    char               padding[124]; // To fill up to 176 bytes for this part // Corrected padding from 116 to 124
};
#pragma pack(pop)

// Ensure the struct has the expected size
static_assert(sizeof(MetaPdu) == TOTAL_PDU_META_SIZE, "MetaPdu size is not correct");


class DataPacket {
public:
    DataPacket() {
        // Ensure meta is zero-initialized
        std::fill_n(reinterpret_cast<std::byte*>(&meta_pdu_), sizeof(meta_pdu_), std::byte{0});
    }

    DataPacket(const std::string& robot_name, uint32_t channel_id, const std::vector<std::byte>& body)
        : body_data_(body)
    {
        std::fill_n(reinterpret_cast<std::byte*>(&meta_pdu_), sizeof(meta_pdu_), std::byte{0});
        set_robot_name(robot_name);
        set_channel_id(channel_id);
    }

    explicit DataPacket(const MetaPdu& meta, const std::vector<std::byte>& body)
        : meta_pdu_(meta), body_data_(body) {}

    // Setters
    void set_hako_time_usec(int64_t time_usec) { meta_pdu_.hako_time_us = time_usec; }
    void set_asset_time_usec(int64_t time_usec) { meta_pdu_.asset_time_us = time_usec; }
    void set_real_time_usec(int64_t time_usec) { meta_pdu_.real_time_us = time_usec; }
    
    void set_robot_name(const std::string& name) {
        std::strncpy(meta_pdu_.robot_name, name.c_str(), sizeof(meta_pdu_.robot_name) - 1);
        meta_pdu_.robot_name[sizeof(meta_pdu_.robot_name) - 1] = '\0';
    }

    void set_channel_id(uint32_t channel_id) { meta_pdu_.channel_id = channel_id; }
    void set_pdu_data(const std::vector<std::byte>& data) { body_data_ = data; }

    // Getters
    std::string get_robot_name() const { return std::string(meta_pdu_.robot_name); }
    uint32_t get_channel_id() const { return meta_pdu_.channel_id; }
    const std::vector<std::byte>& get_pdu_data() const { return body_data_; }
    const MetaPdu& get_meta() const { return meta_pdu_; }
    bool is_pdu_data_type(const std::string& version) const noexcept {
        if (version == "v1") {
            if (body_data_.size() < sizeof(uint32_t)) {
                return false;
            }
            const uint32_t type = read_le32(body_data_.data());
            return type == static_cast<uint32_t>(MetaRequestType::PDU_DATA_TYPE);
        }
        return meta_pdu_.meta_request_type == static_cast<uint32_t>(MetaRequestType::PDU_DATA_TYPE);
    }

    // Encode/Decode
    std::vector<std::byte> encode(const std::string& version = "v2", MetaRequestType request_type = PDU_DATA_TYPE) const {
        if (version == "v1") {
            return encode_v1(request_type);
        }
        return encode_v2(request_type);
    }

    static std::unique_ptr<DataPacket> decode(const std::vector<std::byte>& data, const std::string& version = "v2") {
        if (version == "v1") {
            return decode_v1(data);
        }
        return decode_v2(data);
    }

private:
    MetaPdu meta_pdu_;
    std::vector<std::byte> body_data_;

    static uint32_t read_le32(const std::byte* data) noexcept {
        return static_cast<uint32_t>(std::to_integer<unsigned char>(data[0]))
            | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[1])) << 8)
            | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[2])) << 16)
            | (static_cast<uint32_t>(std::to_integer<unsigned char>(data[3])) << 24);
    }

    static void write_le32(std::byte* out, uint32_t value) noexcept {
        out[0] = static_cast<std::byte>(value & 0xFF);
        out[1] = static_cast<std::byte>((value >> 8) & 0xFF);
        out[2] = static_cast<std::byte>((value >> 16) & 0xFF);
        out[3] = static_cast<std::byte>((value >> 24) & 0xFF);
    }

    static uint16_t bswap16(uint16_t value) noexcept {
        return static_cast<uint16_t>((value >> 8) | (value << 8));
    }

    static uint32_t bswap32(uint32_t value) noexcept {
        return ((value & 0x000000FFu) << 24)
            | ((value & 0x0000FF00u) << 8)
            | ((value & 0x00FF0000u) >> 8)
            | ((value & 0xFF000000u) >> 24);
    }

    static uint64_t bswap64(uint64_t value) noexcept {
        return ((value & 0x00000000000000FFULL) << 56)
            | ((value & 0x000000000000FF00ULL) << 40)
            | ((value & 0x0000000000FF0000ULL) << 24)
            | ((value & 0x00000000FF000000ULL) << 8)
            | ((value & 0x000000FF00000000ULL) >> 8)
            | ((value & 0x0000FF0000000000ULL) >> 24)
            | ((value & 0x00FF000000000000ULL) >> 40)
            | ((value & 0xFF00000000000000ULL) >> 56);
    }

    static uint16_t to_le16(uint16_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap16(value);
    #else
        return value;
    #endif
    }

    static uint32_t to_le32(uint32_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap32(value);
    #else
        return value;
    #endif
    }

    static uint64_t to_le64(uint64_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap64(value);
    #else
        return value;
    #endif
    }

    static uint16_t from_le16(uint16_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap16(value);
    #else
        return value;
    #endif
    }

    static uint32_t from_le32(uint32_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap32(value);
    #else
        return value;
    #endif
    }

    static uint64_t from_le64(uint64_t value) noexcept {
    #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        return bswap64(value);
    #else
        return value;
    #endif
    }

    // V2 Implementation
    std::vector<std::byte> encode_v2(MetaRequestType request_type) const {
        MetaPdu meta_to_send = meta_pdu_;

        uint32_t body_len = body_data_.size();
        meta_to_send.magicno = to_le32(HAKO_META_MAGIC);
        meta_to_send.version = to_le16(HAKO_META_VER_V2);
        meta_to_send.flags = to_le32(0);
        meta_to_send.meta_request_type = to_le32(static_cast<uint32_t>(request_type));
        meta_to_send.body_len = to_le32(body_len);
        meta_to_send.total_len = to_le32((META_V2_FIXED_SIZE - 4) + body_len);
        meta_to_send.hako_time_us = static_cast<int64_t>(to_le64(static_cast<uint64_t>(meta_to_send.hako_time_us)));
        meta_to_send.asset_time_us = static_cast<int64_t>(to_le64(static_cast<uint64_t>(meta_to_send.asset_time_us)));
        meta_to_send.real_time_us = static_cast<int64_t>(to_le64(static_cast<uint64_t>(meta_to_send.real_time_us)));
        meta_to_send.channel_id = to_le32(meta_to_send.channel_id);

        std::vector<std::byte> encoded_data(sizeof(MetaPdu));
        std::memcpy(encoded_data.data(), &meta_to_send, sizeof(MetaPdu));
        encoded_data.insert(encoded_data.end(), body_data_.begin(), body_data_.end());

        return encoded_data;
    }

    static std::unique_ptr<DataPacket> decode_v2(const std::vector<std::byte>& data) {
        if (data.size() < sizeof(MetaPdu)) {
            return nullptr;
        }

        MetaPdu received_meta;
        std::memcpy(&received_meta, data.data(), sizeof(MetaPdu));

        received_meta.magicno = from_le32(received_meta.magicno);
        received_meta.version = from_le16(received_meta.version);
        if (received_meta.magicno != HAKO_META_MAGIC || received_meta.version != HAKO_META_VER_V2) {
            return nullptr;
        }

        received_meta.flags = from_le32(received_meta.flags);
        received_meta.meta_request_type = from_le32(received_meta.meta_request_type);
        received_meta.total_len = from_le32(received_meta.total_len);
        received_meta.body_len = from_le32(received_meta.body_len);
        received_meta.hako_time_us = static_cast<int64_t>(from_le64(static_cast<uint64_t>(received_meta.hako_time_us)));
        received_meta.asset_time_us = static_cast<int64_t>(from_le64(static_cast<uint64_t>(received_meta.asset_time_us)));
        received_meta.real_time_us = static_cast<int64_t>(from_le64(static_cast<uint64_t>(received_meta.real_time_us)));
        received_meta.channel_id = from_le32(received_meta.channel_id);

        size_t expected_body_size = received_meta.body_len;
        size_t actual_body_size = data.size() - sizeof(MetaPdu);

        if (actual_body_size < expected_body_size) {
             return nullptr; // Incomplete packet
        }

        std::vector<std::byte> body(
            data.begin() + sizeof(MetaPdu), 
            data.begin() + sizeof(MetaPdu) + expected_body_size
        );

        return std::make_unique<DataPacket>(received_meta, body);
    }

    // V1 Implementation
    std::vector<std::byte> encode_v1(MetaRequestType request_type) const {
        std::string name_str(meta_pdu_.robot_name);
        uint32_t name_len = static_cast<uint32_t>(name_str.length());
        uint32_t body_len = static_cast<uint32_t>(body_data_.size());
        uint32_t total_body_len = body_len + 4;
        uint32_t header_len = 4 + name_len + 4 + total_body_len;
        uint32_t total_len = 4 + header_len;

        std::vector<std::byte> result(total_len);
        size_t offset = 0;

        write_le32(result.data() + offset, header_len);
        offset += 4;
        write_le32(result.data() + offset, name_len);
        offset += 4;

        const std::byte* name_bytes = reinterpret_cast<const std::byte*>(name_str.c_str());
        std::memcpy(result.data() + offset, name_bytes, name_len);
        offset += name_len;

        write_le32(result.data() + offset, meta_pdu_.channel_id);
        offset += 4;

        write_le32(result.data() + offset, static_cast<uint32_t>(request_type));
        offset += 4;

        if (!body_data_.empty()) {
            std::memcpy(result.data() + offset, body_data_.data(), body_data_.size());
        }

        return result;
    }

    static std::unique_ptr<DataPacket> decode_v1(const std::vector<std::byte>& data) {
        if (data.size() < 4) return nullptr;

        size_t index = 0;
        auto read_uint32_le = [&](uint32_t& val) {
            if (index + sizeof(uint32_t) > data.size()) return false;
            val = read_le32(reinterpret_cast<const std::byte*>(data.data() + index));
            index += sizeof(uint32_t);
            return true;
        };

        uint32_t header_len = 0;
        uint32_t name_len = 0;
        uint32_t channel_id = 0;
        if (!read_uint32_le(header_len)) return nullptr;
        if (data.size() < 4 + header_len) return nullptr;
        if (!read_uint32_le(name_len)) return nullptr;
        if (index + name_len + 4 > data.size()) return nullptr;
        if (header_len < (4 + name_len + 4)) return nullptr;

        std::string robot_name(reinterpret_cast<const char*>(data.data() + index), name_len);
        index += name_len;

        if (!read_uint32_le(channel_id)) return nullptr;

        std::vector<std::byte> body(data.begin() + index, data.end());

        return std::make_unique<DataPacket>(robot_name, channel_id, body);
    }
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
