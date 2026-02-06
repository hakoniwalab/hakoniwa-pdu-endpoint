#include "hakoniwa/pdu/endpoint.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    hakoniwa::pdu::Endpoint client("tcp_client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    if (client.open("config/sample/endpoint_tcp_client.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open TCP client endpoint" << std::endl;
        return 1;
    }
    if (client.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start TCP client endpoint" << std::endl;
        return 1;
    }

    hakoniwa::pdu::PduResolvedKey key;
    key.robot = "ExampleRobot";
    key.channel_id = 1;

    std::vector<std::byte> payload = {
        std::byte('p'), std::byte('i'), std::byte('n'), std::byte('g')
    };

    if (client.send(key, payload) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to send" << std::endl;
    }

    std::vector<std::byte> buffer(256);
    size_t received = 0;
    for (int i = 0; i < 50; ++i) {
        if (client.recv(key, buffer, received) == HAKO_PDU_ERR_OK) {
            std::cout << "Received " << received << " bytes" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    (void)client.stop();
    (void)client.close();
    return 0;
}
