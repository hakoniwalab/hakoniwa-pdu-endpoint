#include "hakoniwa/pdu/udp_endpoint.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Helper to find an available port
int find_available_port() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) < 0) {
        close(sock);
        return -1;
    }

    int port = ntohs(addr.sin_port);
    close(sock);
    return port;
}

// Helper to create a temporary config file
void create_config_file(const std::string& filepath, const std::string& content) {
    std::ofstream file(filepath);
    file << content;
    file.close();
}

class UdpEndpointTest : public ::testing::Test {
protected:
    std::string server_config_path = "server_config.json";
    std::string client_config_path = "client_config.json";
    int server_port = -1;
    int client_port = -1;

    void SetUp() override {
        server_port = find_available_port();
        client_port = find_available_port();
        ASSERT_NE(server_port, -1);
        ASSERT_NE(client_port, -1);
        ASSERT_NE(server_port, client_port);
    }

    void TearDown() override {
        unlink(server_config_path.c_str());
        unlink(client_config_path.c_str());
    }
};

TEST_F(UdpEndpointTest, InOutCommunication) {
    // Server (inout, no fixed remote)
    std::string server_config = R"({
        "protocol": "udp",
        "name": "server",
        "direction": "inout",
        "local": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( },
        "options": { "timeout_ms": 100, "blocking": true }
    })";
    create_config_file(server_config_path, server_config);

    // Client (inout, with fixed remote)
    std::string client_config = R"({
        "protocol": "udp",
        "name": "client",
        "direction": "inout",
        "local": { "address": "127.0.0.1", "port": )" + std::to_string(client_port) + R"( },
        "remote": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( },
        "options": { "timeout_ms": 100, "blocking": true }
    })";
    create_config_file(client_config_path, client_config);

    hakoniwa::pdu::UdpEndpoint server("server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::UdpEndpoint client("client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(server.open(server_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open(client_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    // Client sends "ping" to server
    const std::string ping = "ping";
    ASSERT_EQ(client.send(ping.c_str(), ping.length()), HAKO_PDU_ERR_OK);

    // Server receives "ping"
    char server_buffer[16] = {};
    size_t server_received_size = 0;
    ASSERT_EQ(server.recv(server_buffer, sizeof(server_buffer), server_received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_received_size, ping.length());
    ASSERT_EQ(std::string(server_buffer, server_received_size), ping);

    // Server sends "pong" back to client
    const std::string pong = "pong";
    ASSERT_EQ(server.send(pong.c_str(), pong.length()), HAKO_PDU_ERR_OK);

    // Client receives "pong"
    char client_buffer[16] = {};
    size_t client_received_size = 0;
    ASSERT_EQ(client.recv(client_buffer, sizeof(client_buffer), client_received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client_received_size, pong.length());
    ASSERT_EQ(std::string(client_buffer, client_received_size), pong);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
}

TEST_F(UdpEndpointTest, OutInCommunication) {
    // Receiver (in)
    std::string receiver_config = R"({
        "protocol": "udp",
        "name": "receiver",
        "direction": "in",
        "local": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( },
        "options": { "timeout_ms": 100 }
    })";
    create_config_file(server_config_path, receiver_config);

    // Sender (out)
    std::string sender_config = R"({
        "protocol": "udp",
        "name": "sender",
        "direction": "out",
        "remote": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( }
    })";
    create_config_file(client_config_path, sender_config);

    hakoniwa::pdu::UdpEndpoint receiver("receiver", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::UdpEndpoint sender("sender", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(receiver.open(server_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.open(client_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(receiver.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.start(), HAKO_PDU_ERR_OK);

    const std::string data = "test_data";
    ASSERT_EQ(sender.send(data.c_str(), data.length()), HAKO_PDU_ERR_OK);

    char buffer[32] = {};
    size_t received_size = 0;
    ASSERT_EQ(receiver.recv(buffer, sizeof(buffer), received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(received_size, data.length());
    ASSERT_EQ(std::string(buffer, received_size), data);

    ASSERT_EQ(receiver.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.close(), HAKO_PDU_ERR_OK);
}
