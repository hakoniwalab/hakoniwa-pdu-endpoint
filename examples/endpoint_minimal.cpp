#include "hakoniwa/pdu/endpoint.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
void print_usage(const char* prog)
{
    std::cerr << "Usage: " << prog << " <endpoint_config> <send|recv> [count_or_seconds]\n";
}

std::vector<std::byte> to_bytes(const std::string& s)
{
    std::vector<std::byte> out(s.size());
    std::memcpy(out.data(), s.data(), s.size());
    return out;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const std::string config_path = argv[1];
    const std::string mode = argv[2];
    const int count_or_seconds = (argc >= 4) ? std::stoi(argv[3]) : 5;

    hakoniwa::pdu::Endpoint endpoint("tutorial_endpoint", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    auto err = endpoint.open(config_path);
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "open failed: " << static_cast<int>(err) << "\n";
        return 1;
    }
    err = endpoint.start();
    if (err != HAKO_PDU_ERR_OK) {
        std::cerr << "start failed: " << static_cast<int>(err) << "\n";
        return 1;
    }

    hakoniwa::pdu::PduResolvedKey key{"TutorialRobo", 0};
    endpoint.subscribe_on_recv_callback(key, [](const hakoniwa::pdu::PduResolvedKey&,
                                                std::span<const std::byte> data) {
        std::string text(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << "recv: " << text << "\n";
    });

    if (mode == "send") {
        bool running = false;
        for (int i = 0; i < 50; ++i) { // wait up to ~5s
            if (endpoint.is_running(running) == HAKO_PDU_ERR_OK && running) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!running) {
            std::cerr << "endpoint not running (connect failed)\n";
            return 1;
        }
        for (int i = 0; i < count_or_seconds; ++i) {
            std::string msg = "hello " + std::to_string(i);
            auto bytes = to_bytes(msg);
            err = endpoint.send(key, bytes);
            if (err != HAKO_PDU_ERR_OK) {
                std::cerr << "send failed: " << static_cast<int>(err) << "\n";
                break;
            }
            std::cout << "sent: " << msg << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    } else if (mode == "recv") {
        std::cout << "listening for " << count_or_seconds << " seconds...\n";
        std::this_thread::sleep_for(std::chrono::seconds(count_or_seconds));
    } else {
        print_usage(argv[0]);
    }

    (void)endpoint.stop();
    (void)endpoint.close();
    return 0;
}
