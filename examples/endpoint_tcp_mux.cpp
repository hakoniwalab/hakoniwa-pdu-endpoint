#include "hakoniwa/pdu/endpoint_comm_multiplexer.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    hakoniwa::pdu::EndpointCommMultiplexer mux("tcp_mux", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    if (mux.open("config/sample/endpoint_mux.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open TCP mux" << std::endl;
        return 1;
    }
    if (mux.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start TCP mux" << std::endl;
        return 1;
    }

    std::vector<std::unique_ptr<hakoniwa::pdu::Endpoint>> endpoints;
    while (true) {
        auto batch = mux.take_endpoints();
        for (auto& ep : batch) {
            std::cout << "New endpoint ready" << std::endl;
            endpoints.push_back(std::move(ep));
        }

        // Example: once all expected clients connect, you can break and use endpoints.
        if (mux.is_ready()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Ready endpoints: " << endpoints.size() << std::endl;

    for (auto& ep : endpoints) {
        (void)ep->stop();
        (void)ep->close();
    }

    (void)mux.stop();
    (void)mux.close();
    return 0;
}
