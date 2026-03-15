#include "hakoniwa/pdu/endpoint.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
std::vector<std::byte> to_fixed_u64_payload(std::uint64_t value)
{
    std::vector<std::byte> payload(sizeof(value));
    std::memcpy(payload.data(), &value, sizeof(value));
    return payload;
}
} // namespace

int main()
{
    hakoniwa::pdu::Endpoint endpoint("zenoh_pub_example", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    if (endpoint.open("config/sample/endpoint_zenoh_pub.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open Zenoh publisher endpoint" << std::endl;
        return 1;
    }
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start Zenoh publisher endpoint" << std::endl;
        return 1;
    }

    const hakoniwa::pdu::PduResolvedKey key{"StorageDemo", 0};
    for (std::uint64_t i = 1; i <= 5; ++i) {
        const auto payload = to_fixed_u64_payload(i);
        if (endpoint.send(key, payload) != HAKO_PDU_ERR_OK) {
            std::cerr << "Zenoh send failed" << std::endl;
            return 1;
        }
        std::cout << "published sample_state=" << i << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
