#include "hakoniwa/pdu/endpoint.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    hakoniwa::pdu::Endpoint client("udp_client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    if (client.open("config/tutorial/endpoint_udp_client.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open UDP client endpoint" << std::endl;
        return 1;
    }
    if (client.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start UDP client endpoint" << std::endl;
        return 1;
    }

    hakoniwa::pdu::PduResolvedKey key;
    key.robot = "TutorialRobo";
    key.channel_id = 0;

    std::vector<std::byte> payload = {
        std::byte('u'), std::byte('d'), std::byte('p')
    };

    if (client.send(key, payload) != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to send" << std::endl;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    (void)client.stop();
    (void)client.close();
    return 0;
}
