#include "hakoniwa/pdu/endpoint.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
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
    hakoniwa::pdu::Endpoint endpoint("storage_latest_example", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    if (endpoint.open("config/sample/endpoint_storage_latest.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open storage latest endpoint" << std::endl;
        return 1;
    }
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start storage latest endpoint" << std::endl;
        return 1;
    }

    const hakoniwa::pdu::PduResolvedKey key{"StorageDemo", 0};
    const auto first = to_fixed_u64_payload(1);
    const auto second = to_fixed_u64_payload(2);

    if (endpoint.send(key, first) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to send first latest payload" << std::endl;
        return 1;
    }
    if (endpoint.send(key, second) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to send second latest payload" << std::endl;
        return 1;
    }

    (void)endpoint.stop();
    (void)endpoint.close();

    std::cout << "Wrote two fixed-size packets; latest file keeps only the newest one." << std::endl;
    std::cout << "Inspect with: build/tools/hako_pdu_storage_debug config/runtime/storage_latest.bin" << std::endl;
    return 0;
}
