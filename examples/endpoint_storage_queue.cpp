#include "hakoniwa/pdu/endpoint.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> to_bytes(const std::string& text)
{
    std::vector<std::byte> out(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        out[i] = static_cast<std::byte>(text[i]);
    }
    return out;
}
} // namespace

int main()
{
    hakoniwa::pdu::Endpoint endpoint("storage_queue_example", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    if (endpoint.open("config/sample/endpoint_storage_queue.json") != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to open storage queue endpoint" << std::endl;
        return 1;
    }
    if (endpoint.start() != HAKO_PDU_ERR_OK) {
        std::cerr << "Failed to start storage queue endpoint" << std::endl;
        return 1;
    }

    const hakoniwa::pdu::PduResolvedKey key{"StorageDemo", 0};
    const std::vector<std::string> messages = {
        "queue-0",
        "queue-1",
        "queue-2"
    };
    for (const auto& message : messages) {
        if (endpoint.send(key, to_bytes(message)) != HAKO_PDU_ERR_OK) {
            std::cerr << "Failed to send queue message" << std::endl;
            return 1;
        }
        std::cout << "sent: " << message << std::endl;
    }

    (void)endpoint.stop();
    (void)endpoint.close();

    std::cout << "Inspect with: build/tools/hako_pdu_storage_debug config/runtime/storage_queue.bin" << std::endl;
    return 0;
}
