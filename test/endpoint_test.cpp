#include <gtest/gtest.h>
#include "hakoniwa/pdu/endpoint.hpp"
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <fstream>
#include <unistd.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>

// Test Utilities
namespace {
    // Finds an available UDP or TCP port.
    int find_available_port(int type) {
        int sock = socket(AF_INET, type, 0);
        if (sock < 0) return -1;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0; // 0 means assign any free port

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

    // Creates a temporary, dynamic config file for testing.
    bool create_dynamic_config(const std::string& tmp_path, const std::string& template_path, int port, int remote_port = 0) {
        std::ifstream ifs(template_path);
        if (!ifs.is_open()) return false;
        
        nlohmann::json config;
        try {
            ifs >> config;
            if (config.contains("local") && config["local"].contains("port")) {
                config["local"]["port"] = port;
            }
            if (remote_port > 0 && config.contains("remote") && config["remote"].contains("port")) {
                config["remote"]["port"] = remote_port;
            }
            
            std::ofstream ofs(tmp_path);
            ofs << config.dump(4);
            return true;
        } catch (...) {
            return false;
        }
    }
}

class EndpointTest : public ::testing::Test {
protected:
    hakoniwa::pdu::PduResolvedKey create_key(const std::string& robot_name, hako_pdu_uint32_t channel_id) {
        hakoniwa::pdu::PduResolvedKey key;
        key.robot = robot_name;
        key.channel_id = channel_id;
        return key;
    }
};

TEST_F(EndpointTest, BufferModeTest) {
    hakoniwa::pdu::Endpoint endpoint("buffer_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot1", 1);
    std::vector<std::byte> write_data1 = {(std::byte)0xAA};
    std::vector<std::byte> write_data2 = {(std::byte)0xBB, (std::byte)0xCC};

    ASSERT_EQ(endpoint.send(key, write_data1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, write_data2), HAKO_PDU_ERR_OK);

    std::vector<std::byte> read_buffer(10);
    size_t read_len = 0;
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());
    EXPECT_EQ(read_buffer[0], write_data2[0]);
    EXPECT_EQ(read_buffer[1], write_data2[1]);

    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, QueueModeTest) {
    hakoniwa::pdu::Endpoint endpoint("queue_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot2", 2);
    std::vector<std::byte> write_data1 = {(std::byte)0x11};
    std::vector<std::byte> write_data2 = {(std::byte)0x22};

    ASSERT_EQ(endpoint.send(key, write_data1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, write_data2), HAKO_PDU_ERR_OK);

    std::vector<std::byte> read_buffer(10);
    size_t read_len = 0;

    // Read first item (should be write_data1)
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data1.size());
    EXPECT_EQ(read_buffer[0], write_data1[0]);

    // Read second item (should be write_data2)
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, write_data2.size());
    EXPECT_EQ(read_buffer[0], write_data2[0]);

    // Read again, should be empty
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, TcpCommunicationTest) {
    int server_port = find_available_port(SOCK_STREAM);
    ASSERT_GT(server_port, 0);

    // Create dynamic configs
    const char* tmp_server_comm_path = "temp_tcp_server_comm.json";
    const char* tmp_client_comm_path = "temp_tcp_client_comm.json";
    const char* tmp_server_endpoint_path = "temp_tcp_server_endpoint.json";
    const char* tmp_client_endpoint_path = "temp_tcp_client_endpoint.json";

    ASSERT_TRUE(create_dynamic_config(tmp_server_comm_path, "config/sample/comm/tcp_server_inout_comm.json", server_port));
    ASSERT_TRUE(create_dynamic_config(tmp_client_comm_path, "config/sample/comm/tcp_client_inout_comm.json", 0, server_port));

    create_dynamic_config(tmp_server_endpoint_path, "test/test_endpoint_tcp_server.json", 0, 0);
    create_dynamic_config(tmp_client_endpoint_path, "test/test_endpoint_tcp_client.json", 0, 0);
    
    hakoniwa::pdu::Endpoint server("tcp_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    // Minor hack to point to temp comm files
    {
        std::ifstream ifs(tmp_server_endpoint_path);
        nlohmann::json j;
        ifs >> j;
        j["comm"] = tmp_server_comm_path;
        std::ofstream ofs(tmp_server_endpoint_path);
        ofs << j;
    }
    {
        std::ifstream ifs(tmp_client_endpoint_path);
        nlohmann::json j;
        ifs >> j;
        j["comm"] = tmp_client_comm_path;
        std::ofstream ofs(tmp_client_endpoint_path);
        ofs << j;
    }

    ASSERT_EQ(server.open(tmp_server_endpoint_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open(tmp_client_endpoint_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto key = create_key("robot_tcp", 10);
    std::vector<std::byte> client_msg = {(std::byte)'p', (std::byte)'i', (std::byte)'n', (std::byte)'g'};

    ASSERT_EQ(client.send(key, client_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> server_buf(10);
    size_t server_len = 0;
    ASSERT_EQ(server.recv(key, server_buf, server_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_len, client_msg.size());
    server_buf.resize(server_len);
    EXPECT_EQ(server_buf, client_msg);
    
    std::vector<std::byte> server_msg = {(std::byte)'p', (std::byte)'o', (std::byte)'n', (std::byte)'g'};
    ASSERT_EQ(server.send(key, server_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> client_buf(10);
    size_t client_len = 0;
    ASSERT_EQ(client.recv(key, client_buf, client_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client_len, server_msg.size());
    client_buf.resize(client_len);
    EXPECT_EQ(client_buf, server_msg);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
    
    unlink(tmp_server_comm_path);
    unlink(tmp_client_comm_path);
    unlink(tmp_server_endpoint_path);
    unlink(tmp_client_endpoint_path);
}

TEST_F(EndpointTest, UdpCommunicationTest) {
    int server_port = find_available_port(SOCK_DGRAM);
    ASSERT_GT(server_port, 0);

    const char* tmp_server_comm_path = "temp_udp_server_comm.json";
    const char* tmp_client_comm_path = "temp_udp_client_comm.json";
    const char* tmp_server_endpoint_path = "temp_udp_server_endpoint.json";
    const char* tmp_client_endpoint_path = "temp_udp_client_endpoint.json";

    ASSERT_TRUE(create_dynamic_config(tmp_server_comm_path, "config/sample/comm/udp_inout_comm.json", server_port));
    ASSERT_TRUE(create_dynamic_config(tmp_client_comm_path, "config/sample/comm/udp_client_for_test_comm.json", 0, server_port));

    create_dynamic_config(tmp_server_endpoint_path, "test/test_endpoint_udp_server.json", 0, 0);
    create_dynamic_config(tmp_client_endpoint_path, "test/test_endpoint_udp_client.json", 0, 0);

    {
        std::ifstream ifs(tmp_server_endpoint_path); nlohmann::json j; ifs >> j;
        j["comm"] = tmp_server_comm_path;
        std::ofstream ofs(tmp_server_endpoint_path); ofs << j;
    }
    {
        std::ifstream ifs(tmp_client_endpoint_path); nlohmann::json j; ifs >> j;
        j["comm"] = tmp_client_comm_path;
        std::ofstream ofs(tmp_client_endpoint_path); ofs << j;
    }

    hakoniwa::pdu::Endpoint server("udp_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("udp_client", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(server.open(tmp_server_endpoint_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open(tmp_client_endpoint_path), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto key = create_key("robot_udp", 20);
    std::vector<std::byte> client_msg = {(std::byte)'h', (std::byte)'e', (std::byte)'l', (std::byte)'l', (std::byte)'o'};

    ASSERT_EQ(client.send(key, client_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> server_buf(20);
    size_t server_len = 0;
    ASSERT_EQ(server.recv(key, server_buf, server_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_len, client_msg.size());
    server_buf.resize(server_len);
    EXPECT_EQ(server_buf, client_msg);
    
    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);

    unlink(tmp_server_comm_path);
    unlink(tmp_client_comm_path);
    unlink(tmp_server_endpoint_path);
    unlink(tmp_client_endpoint_path);
}
