#include "hakoniwa/pdu/endpoint.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

namespace {
std::uint64_t from_fixed_u64_payload(std::span<const std::byte> payload)
{
    std::uint64_t value = 0;
    std::memcpy(&value, payload.data(), sizeof(value));
    return value;
}
} // namespace

int main()
{
    hakoniwa::pdu::Endpoint endpoint("mqtt_sub_example", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    if (endpoint.open("config/sample/endpoint_mqtt_sub.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open MQTT subscriber endpoint" << std::endl;
        return 1;
    }
    endpoint.subscribe_on_recv_callback(
        hakoniwa::pdu::PduResolvedKey{"StorageDemo", 0},
        [](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            if (data.size() == sizeof(std::uint64_t)) {
                std::cout << "received sample_state=" << from_fixed_u64_payload(data) << std::endl;
            } else {
                std::cout << "received " << data.size() << " bytes" << std::endl;
            }
        });
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start MQTT subscriber endpoint" << std::endl;
        return 1;
    }

    std::cout << "Waiting for MQTT samples..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
