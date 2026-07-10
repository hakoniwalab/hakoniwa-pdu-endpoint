#include "hakoniwa/pdu/endpoint.hpp"

#include "std_msgs/pdu_cpptype_cdr_conv_UInt64.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <thread>
#include <vector>

namespace {
std::vector<std::uint8_t> to_u8_payload(std::span<const std::byte> src)
{
    std::vector<std::uint8_t> dst(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = static_cast<std::uint8_t>(src[i]);
    }
    return dst;
}
} // namespace

int main(int argc, char* argv[])
{
    const std::string config_path = (argc > 1) ? argv[1] : "config/sample/endpoint_rmw_zenoh_sub.json";

    hakoniwa::pdu::Endpoint endpoint("zenoh_sub_cdr_example", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    if (endpoint.open(config_path) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open Zenoh CDR subscriber endpoint: " << config_path << std::endl;
        return 1;
    }

    endpoint.subscribe_on_recv_callback(
        hakoniwa::pdu::PduResolvedKey{"StorageDemo", 0},
        [](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            hako::pdu::msgs::std_msgs::UInt64Cdr converter;
            HakoCpp_UInt64 value{};
            const auto cdr_payload = to_u8_payload(data);
            if (converter.cdr2cpp(cdr_payload, value)) {
                std::cout << "received sample_state_cdr=" << value.data << " bytes=" << data.size() << std::endl;
            } else {
                std::cout << "received undecodable CDR payload bytes=" << data.size() << std::endl;
            }
        });

    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start Zenoh CDR subscriber endpoint" << std::endl;
        return 1;
    }

    std::cout << "Waiting for Zenoh CDR samples..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(500));

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
