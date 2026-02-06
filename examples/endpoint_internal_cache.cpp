#include "hakoniwa/pdu/endpoint.hpp"
#include <iostream>
#include <vector>

int main() {
    hakoniwa::pdu::Endpoint endpoint("internal_cache", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    if (endpoint.open("config/sample/endpoint_internal_cache.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open internal cache endpoint" << std::endl;
        return 1;
    }
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start internal cache endpoint" << std::endl;
        return 1;
    }

    hakoniwa::pdu::PduResolvedKey key;
    key.robot = "TestRobot";
    key.channel_id = 1;

    std::vector<std::byte> write_data = {std::byte(0x01), std::byte(0x02)};
    if (endpoint.send(key, write_data) != HAKO_PDU_ERR_OK) {
        std::cerr << "Send failed" << std::endl;
        return 1;
    }

    std::vector<std::byte> read_buffer(16);
    size_t read_len = 0;
    if (endpoint.recv(key, read_buffer, read_len) != HAKO_PDU_ERR_OK) {
        std::cerr << "Recv failed" << std::endl;
        return 1;
    }

    std::cout << "Read back " << read_len << " bytes from internal cache" << std::endl;

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
