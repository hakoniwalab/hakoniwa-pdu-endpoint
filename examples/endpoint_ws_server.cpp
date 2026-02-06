#include "hakoniwa/pdu/endpoint.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    hakoniwa::pdu::Endpoint server("ws_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    if (server.open("config/sample/endpoint_websocket_server.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open WebSocket server endpoint" << std::endl;
        return 1;
    }
    if (server.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start WebSocket server endpoint" << std::endl;
        return 1;
    }

    hakoniwa::pdu::PduResolvedKey key;
    key.robot = "ExampleRobot";
    key.channel_id = 1;

    std::vector<std::byte> buffer(256);
    size_t received = 0;

    for (int i = 0; i < 50; ++i) {
        if (server.recv(key, buffer, received) == HAKO_PDU_ERR_OK) {
            std::cout << "Received " << received << " bytes" << std::endl;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    (void)server.stop();
    (void)server.close();
    return 0;
}
