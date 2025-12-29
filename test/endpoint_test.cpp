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
    bool create_dynamic_config(const std::string& tmp_path, const std::string& template_path, int port, int remote_port = 0, const std::string& remote_host = "127.0.0.1", const std::string& remote_path = "/") {
        std::ifstream ifs(template_path);
        if (!ifs.is_open()) return false;
        
        nlohmann::json config;
        try {
            ifs >> config;
            if (config.contains("local") && config["local"].contains("port")) {
                config["local"]["port"] = port;
            }
            if (config.contains("remote")) {
                if (remote_port > 0) {
                    config["remote"]["port"] = remote_port;
                }
                config["remote"]["host"] = remote_host;
                config["remote"]["path"] = remote_path;
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
    hakoniwa::pdu::PduResolvedKey create_key(const std::string& robot_name, HakoPduChannelIdType channel_id) {
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

TEST_F(EndpointTest, PduDefinitionTest) {
    hakoniwa::pdu::Endpoint endpoint("pdu_def_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_pdu_def_endpoint.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduKey key;
    key.robot = "TestRobot";
    key.pdu = "TestPDU";

    // Test getter methods
    EXPECT_EQ(endpoint.get_pdu_size(key), 8);
    EXPECT_EQ(endpoint.get_pdu_channel_id(key), 123);

    // Test non-existent PDU
    hakoniwa::pdu::PduKey bad_key;
    bad_key.robot = "TestRobot";
    bad_key.pdu = "NonExistentPDU";
    EXPECT_EQ(endpoint.get_pdu_size(bad_key), 0);
    EXPECT_EQ(endpoint.get_pdu_channel_id(bad_key), -1);

    // Test send/recv cycle
    std::vector<std::byte> send_data = {
        std::byte(0xDE), std::byte(0xAD), std::byte(0xBE), std::byte(0xEF),
        std::byte(0xCA), std::byte(0xFE), std::byte(0xBA), std::byte(0xBE)
    };
    ASSERT_EQ(endpoint.send(key, send_data), HAKO_PDU_ERR_OK);

    std::vector<std::byte> recv_buffer(10);
    size_t received_size = 0;
    ASSERT_EQ(endpoint.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_OK);

    ASSERT_EQ(received_size, send_data.size());
    for (size_t i = 0; i < received_size; ++i) {
        EXPECT_EQ(recv_buffer[i], send_data[i]);
    }

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, TcpCommunicationTest) {
    int server_port = find_available_port(SOCK_STREAM);
    ASSERT_GT(server_port, 0);

    // Create dynamic configs
    hakoniwa::pdu::Endpoint server("tcp_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(server.open("test/test_endpoint_tcp_server.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_tcp_client.json"), HAKO_PDU_ERR_OK);
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
    
}

TEST_F(EndpointTest, UdpCommunicationTest) {
    int server_port = find_available_port(SOCK_DGRAM);
    ASSERT_GT(server_port, 0);


    hakoniwa::pdu::Endpoint server("udp_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("udp_client", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(server.open("test/test_endpoint_udp_server.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_udp_client.json"), HAKO_PDU_ERR_OK);
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

}

TEST_F(EndpointTest, WebSocketCommunicationTest) {
    int server_port = find_available_port(SOCK_STREAM);
    ASSERT_GT(server_port, 0);

    hakoniwa::pdu::Endpoint server("ws_server", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("ws_client", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);


    ASSERT_EQ(server.open("test/test_endpoint_ws_server.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_ws_client.json"), HAKO_PDU_ERR_OK);
    
    // Start server first, then client connects
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Give server time to start accepting

    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Give client time to connect

    auto key = create_key("robot_ws", 30);
    std::vector<std::byte> client_msg = {(std::byte)'W', (std::byte)'e', (std::byte)'b', (std::byte)'S', (std::byte)'o', (std::byte)'c', (std::byte)'k', (std::byte)'e', (std::byte)'t'};

    // Client sends message to server
    ASSERT_EQ(client.send(key, client_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> server_buf(client_msg.size());
    size_t server_len = 0;
    ASSERT_EQ(server.recv(key, server_buf, server_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_len, client_msg.size());
    EXPECT_EQ(server_buf, client_msg);
    
    std::vector<std::byte> server_msg = {(std::byte)'H', (std::byte)'e', (std::byte)'l', (std::byte)'l', (std::byte)'o', (std::byte)' ', (std::byte)'C', (std::byte)'l', (std::byte)'i', (std::byte)'e', (std::byte)'n', (std::byte)'t'};

    // Server sends message to client
    ASSERT_EQ(server.send(key, server_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> client_buf(server_msg.size());
    size_t client_len = 0;
    ASSERT_EQ(client.recv(key, client_buf, client_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client_len, server_msg.size());
    EXPECT_EQ(client_buf, server_msg);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
 
}

TEST_F(EndpointTest, WebSocketCommunicationInOutTest) {
    int server_port = find_available_port(SOCK_STREAM);
    ASSERT_GT(server_port, 0);


    hakoniwa::pdu::Endpoint server("ws_server_inout", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("ws_client_inout", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);


    ASSERT_EQ(server.open("test/test_endpoint_ws_server_inout.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_ws_client_inout.json"), HAKO_PDU_ERR_OK);
    
    // Start server first, then client connects
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Give server time to start accepting

    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Give client time to connect

    auto key = create_key("robot_ws_inout", 40);
    std::vector<std::byte> client_msg = {(std::byte)'I', (std::byte)'N', (std::byte)'O', (std::byte)'U', (std::byte)'T', (std::byte)' ', (std::byte)'C', (std::byte)'L', (std::byte)'I', (std::byte)'E', (std::byte)'N', (std::byte)'T'};

    // Client sends message to server
    ASSERT_EQ(client.send(key, client_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> server_buf(client_msg.size());
    size_t server_len = 0;
    ASSERT_EQ(server.recv(key, server_buf, server_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_len, client_msg.size());
    EXPECT_EQ(server_buf, client_msg);
    
    std::vector<std::byte> server_msg = {(std::byte)'I', (std::byte)'N', (std::byte)'O', (std::byte)'U', (std::byte)'T', (std::byte)' ', (std::byte)'S', (std::byte)'E', (std::byte)'R', (std::byte)'V', (std::byte)'E', (std::byte)'R'};

    // Server sends message to client
    ASSERT_EQ(server.send(key, server_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> client_buf(server_msg.size());
    size_t client_len = 0;
    ASSERT_EQ(client.recv(key, client_buf, client_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client_len, server_msg.size());
    EXPECT_EQ(client_buf, server_msg);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
    
}


