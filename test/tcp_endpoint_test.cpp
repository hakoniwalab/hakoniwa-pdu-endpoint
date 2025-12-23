#include "hakoniwa/pdu/tcp_endpoint.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <unistd.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

// Helper to find an available TCP port
int find_available_tcp_port() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) < 0) {
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

class TcpEndpointTest : public ::testing::Test {
protected:
    std::string server_config_path = "tcp_server_config.json";
    std::string client_config_path = "tcp_client_config.json";
    int server_port = -1;

    void SetUp() override {
        server_port = find_available_tcp_port();
        ASSERT_NE(server_port, -1);
    }

    void TearDown() override {
        unlink(server_config_path.c_str());
        unlink(client_config_path.c_str());
    }
};

TEST_F(TcpEndpointTest, InOutCommunicationServerClient) {
    std::string server_config = R"({
        "protocol": "tcp",
        "name": "server",
        "direction": "inout",
        "role": "server",
        "local": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( }
    })";
    create_config_file(server_config_path, server_config);

    std::string client_config = R"({
        "protocol": "tcp",
        "name": "client",
        "direction": "inout",
        "role": "client",
        "remote": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( }
    })";
    create_config_file(client_config_path, client_config);

    hakoniwa::pdu::TcpEndpoint server("server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::TcpEndpoint client("client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(server.open(server_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open(client_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    const std::string ping = "ping";
    ASSERT_EQ(client.send(ping.c_str(), ping.size()), HAKO_PDU_ERR_OK);

    std::vector<char> server_buffer(ping.size());
    size_t server_received_size = 0;
    ASSERT_EQ(server.recv(server_buffer.data(), server_buffer.size(), server_received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_received_size, ping.size());
    ASSERT_EQ(std::string(server_buffer.data(), server_received_size), ping);

    const std::string pong = "pong";
    ASSERT_EQ(server.send(pong.c_str(), pong.size()), HAKO_PDU_ERR_OK);

    std::vector<char> client_buffer(pong.size());
    size_t client_received_size = 0;
    ASSERT_EQ(client.recv(client_buffer.data(), client_buffer.size(), client_received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client_received_size, pong.size());
    ASSERT_EQ(std::string(client_buffer.data(), client_received_size), pong);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
}

TEST_F(TcpEndpointTest, OutInCommunication) {
    std::string server_config = R"({
        "protocol": "tcp",
        "name": "receiver",
        "direction": "in",
        "role": "server",
        "local": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( }
    })";
    create_config_file(server_config_path, server_config);

    std::string client_config = R"({
        "protocol": "tcp",
        "name": "sender",
        "direction": "out",
        "role": "client",
        "remote": { "address": "127.0.0.1", "port": )" + std::to_string(server_port) + R"( }
    })";
    create_config_file(client_config_path, client_config);

    hakoniwa::pdu::TcpEndpoint receiver("receiver", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::TcpEndpoint sender("sender", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(receiver.open(server_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.open(client_config_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(receiver.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.start(), HAKO_PDU_ERR_OK);

    const std::string data = "test_data";
    ASSERT_EQ(sender.send(data.c_str(), data.size()), HAKO_PDU_ERR_OK);

    std::vector<char> buffer(data.size());
    size_t received_size = 0;
    ASSERT_EQ(receiver.recv(buffer.data(), buffer.size(), received_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(received_size, data.size());
    ASSERT_EQ(std::string(buffer.data(), received_size), data);

    ASSERT_EQ(receiver.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(sender.close(), HAKO_PDU_ERR_OK);
}
