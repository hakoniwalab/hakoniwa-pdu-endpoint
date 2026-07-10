#include "hakoniwa/pdu/endpoint.hpp"

#include "std_msgs/pdu_cpptype_cdr_conv_UInt64.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

namespace {
std::vector<std::byte> to_byte_payload(const std::vector<std::uint8_t>& src)
{
    std::vector<std::byte> dst(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = static_cast<std::byte>(src[i]);
    }
    return dst;
}
} // namespace

int main(int argc, char* argv[])
{
    const std::string config_path = (argc > 1) ? argv[1] : "config/sample/endpoint_rmw_zenoh_pub.json";

    hakoniwa::pdu::Endpoint endpoint("zenoh_pub_cdr_example", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    if (endpoint.open(config_path) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open Zenoh CDR publisher endpoint: " << config_path << std::endl;
        return 1;
    }
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start Zenoh CDR publisher endpoint" << std::endl;
        return 1;
    }

    hako::pdu::msgs::std_msgs::UInt64Cdr converter;
    const hakoniwa::pdu::PduResolvedKey key{"StorageDemo", 0};
    for (std::uint64_t i = 1; i <= 500; ++i) {
        HakoCpp_UInt64 value{};
        value.data = i;

        std::vector<std::uint8_t> cdr_payload;
        if (converter.cpp2cdr(value, cdr_payload) < 0) {
            std::cerr << "UInt64 CDR conversion failed" << std::endl;
            return 1;
        }

        const auto payload = to_byte_payload(cdr_payload);
        if (endpoint.send(key, std::span<const std::byte>(payload.data(), payload.size())) != HAKO_PDU_ERR_OK) {
            std::cerr << "Zenoh CDR send failed" << std::endl;
            return 1;
        }
        std::cout << "published sample_state_cdr=" << i << " bytes=" << payload.size() << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
