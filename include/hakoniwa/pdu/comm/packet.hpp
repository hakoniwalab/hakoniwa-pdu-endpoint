#pragma once

#include <cstdint>
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

    // Encode/Decode
    std::vector<std::byte> encode(const std::string& version = "v2", MetaRequestType request_type = PDU_DATA_TYPE) const {
        if (version == "v1") {
            return encode_v1();
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

    // V2 Implementation
    std::vector<std::byte> encode_v2(MetaRequestType request_type) const {
        MetaPdu meta_to_send = meta_pdu_;

        uint32_t body_len = body_data_.size();
        meta_to_send.magicno = HAKO_META_MAGIC;
        meta_to_send.version = HAKO_META_VER_V2;
        meta_to_send.flags = 0;
        meta_to_send.meta_request_type = request_type;
        meta_to_send.body_len = body_len;
        meta_to_send.total_len = (META_V2_FIXED_SIZE - 4) + body_len;

        // Network byte order conversion
        meta_to_send.magicno = htonl(meta_to_send.magicno);
        meta_to_send.version = htons(meta_to_send.version);
        meta_to_send.flags = htonl(meta_to_send.flags);
        meta_to_send.meta_request_type = htonl(meta_to_send.meta_request_type);
        meta_to_send.total_len = htonl(meta_to_send.total_len);
        meta_to_send.body_len = htonl(meta_to_send.body_len);
        meta_to_send.channel_id = htonl(meta_to_send.channel_id);
        // Assuming timestamps are already in a network-compatible format or don't need conversion.

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

        // Network byte order conversion
        received_meta.magicno = ntohl(received_meta.magicno);
        received_meta.version = ntohs(received_meta.version);

        if (received_meta.magicno != HAKO_META_MAGIC || received_meta.version != HAKO_META_VER_V2) {
            return nullptr;
        }
        
        received_meta.flags = ntohl(received_meta.flags);
        received_meta.meta_request_type = ntohl(received_meta.meta_request_type);
        received_meta.total_len = ntohl(received_meta.total_len);
        received_meta.body_len = ntohl(received_meta.body_len);
        received_meta.channel_id = ntohl(received_meta.channel_id);

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
    std::vector<std::byte> encode_v1() const {
        std::string name_str(meta_pdu_.robot_name);
        uint32_t name_len = name_str.length();
        uint32_t header_len = 4 + name_len + 4; // name_len_field + name + channel_id_field
        
        std::vector<std::byte> result;
        result.reserve(4 + header_len + body_data_.size());

        auto append_uint32 = [&](uint32_t val) {
            val = htonl(val); // Assuming network byte order for v1 too
            const std::byte* bytes = reinterpret_cast<const std::byte*>(&val);
            result.insert(result.end(), bytes, bytes + sizeof(val));
        };
        
        append_uint32(header_len);
        append_uint32(name_len);
        
        const std::byte* name_bytes = reinterpret_cast<const std::byte*>(name_str.c_str());
        result.insert(result.end(), name_bytes, name_bytes + name_len);
        
        append_uint32(meta_pdu_.channel_id);
        result.insert(result.end(), body_data_.begin(), body_data_.end());

        return result;
    }

    static std::unique_ptr<DataPacket> decode_v1(const std::vector<std::byte>& data) {
        if (data.size() < 4) return nullptr;

        size_t index = 0;
        auto read_uint32 = [&](uint32_t& val) {
            if (index + sizeof(uint32_t) > data.size()) return false;
            std::memcpy(&val, data.data() + index, sizeof(uint32_t));
            val = ntohl(val);
            index += sizeof(uint32_t);
            return true;
        };
        
        uint32_t header_len, name_len, channel_id;
        if (!read_uint32(header_len)) return nullptr;
        if (!read_uint32(name_len)) return nullptr;
        if (index + name_len + 4 > data.size()) return nullptr;
        
        std::string robot_name(reinterpret_cast<const char*>(data.data() + index), name_len);
        index += name_len;

        if (!read_uint32(channel_id)) return nullptr;

        std::vector<std::byte> body(data.begin() + index, data.end());

        return std::make_unique<DataPacket>(robot_name, channel_id, body);
    }
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
