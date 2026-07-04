#include <gtest/gtest.h>
#include "hakoniwa/pdu/c_endpoint.h"
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
#include <sys/wait.h>
#include <signal.h>
#include <atomic>
#include <iostream>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <sstream>
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include "hakoniwa/pdu/comm/packet.hpp"
#include "hakoniwa/pdu/comm/storage_format.hpp"
#include "hakoniwa/pdu/endpoint_comm_multiplexer.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include <filesystem>

// Test Utilities
namespace {
    using StorageHeaderV1 = hakoniwa::pdu::comm::storage::StorageHeader;
    using StorageEntryV1 = hakoniwa::pdu::comm::storage::StorageEntry;

    // Finds an available UDP or TCP port.
    int find_available_port(int type) {
        int sock = socket(AF_INET, type, 0);
        if (sock < 0) {
            std::cerr << "find_available_port: socket() failed: " << std::strerror(errno) << std::endl;
            return -1;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = 0; // 0 means assign any free port

        if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::cerr << "find_available_port: bind() failed: " << std::strerror(errno) << std::endl;
            close(sock);
            return -1;
        }

        socklen_t addr_len = sizeof(addr);
        if (getsockname(sock, reinterpret_cast<struct sockaddr*>(&addr), &addr_len) < 0) {
            std::cerr << "find_available_port: getsockname() failed: " << std::strerror(errno) << std::endl;
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

    std::vector<std::vector<std::byte>> read_storage_packets(const std::filesystem::path& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return {};
        }
        StorageHeaderV1 header{};
        if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return {};
        }
        if (header.mode == 1) {
            ifs.seekg(static_cast<std::streamoff>(header.data_offset), std::ios::beg);
        } else {
            ifs.seekg(0, std::ios::beg);
        }
        std::vector<std::vector<std::byte>> packets;
        while (true) {
            char len_buf[4];
            if (!ifs.read(len_buf, sizeof(len_buf))) {
                break;
            }
            const std::uint32_t size =
                static_cast<std::uint32_t>(static_cast<unsigned char>(len_buf[0])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(len_buf[1])) << 8) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(len_buf[2])) << 16) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(len_buf[3])) << 24);
            std::vector<std::byte> packet(size);
            if (!ifs.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()))) {
                return {};
            }
            packets.push_back(std::move(packet));
        }
        return packets;
    }

    std::vector<std::byte> read_latest_packet(const std::filesystem::path& path, std::size_t index_number) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return {};
        }
        StorageHeaderV1 header{};
        if (!ifs.read(reinterpret_cast<char*>(&header), sizeof(header))) {
            return {};
        }
        if (index_number >= header.key_count) {
            return {};
        }
        ifs.seekg(static_cast<std::streamoff>(header.index_offset + (sizeof(StorageEntryV1) * index_number)), std::ios::beg);
        StorageEntryV1 entry{};
        if (!ifs.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
            return {};
        }
        std::vector<std::byte> packet(entry.packet_size);
        ifs.seekg(static_cast<std::streamoff>(entry.packet_offset), std::ios::beg);
        if (!ifs.read(reinterpret_cast<char*>(packet.data()), static_cast<std::streamsize>(packet.size()))) {
            return {};
        }
        return packet;
    }

    void write_queue_storage_header(std::fstream& fsio, std::uint16_t packet_version, std::uint64_t file_size) {
        StorageHeaderV1 header{};
        header.magic = hakoniwa::pdu::comm::storage::kStorageHeaderMagic;
        header.version = hakoniwa::pdu::comm::storage::kStorageVersion;
        header.mode = hakoniwa::pdu::comm::storage::kStorageModeQueue;
        header.packet_version = packet_version;
        header.index_offset = sizeof(StorageHeaderV1);
        header.data_offset = sizeof(StorageHeaderV1);
        header.file_size = file_size;
        fsio.seekp(0, std::ios::beg);
        fsio.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }

    std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) {
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    std::string find_executable_on_path(const std::string& exe_name) {
        const char* path_env = std::getenv("PATH");
        if (path_env == nullptr) {
            return {};
        }
        std::stringstream ss(path_env);
        std::string dir;
        while (std::getline(ss, dir, ':')) {
            if (dir.empty()) {
                continue;
            }
            const auto candidate = std::filesystem::path(dir) / exe_name;
            if (access(candidate.c_str(), X_OK) == 0) {
                return candidate.string();
            }
        }
        return {};
    }

    pid_t launch_mosquitto(const std::string& exe_path, const std::filesystem::path& config_path) {
        const pid_t pid = fork();
        if (pid != 0) {
            return pid;
        }

        execl(exe_path.c_str(), exe_path.c_str(), "-c", config_path.c_str(), nullptr);
        _exit(127);
    }

    void stop_child_process(pid_t pid) {
        if (pid <= 0) {
            return;
        }
        kill(pid, SIGTERM);
        int status = 0;
        waitpid(pid, &status, 0);
    }

    struct CEndpointRecvCapture {
        bool called{false};
        std::string robot;
        uint32_t channel_id{0};
        std::vector<std::uint8_t> payload;
    };

    void c_endpoint_on_recv_capture(
        void* user_data,
        const hako_pdu_resolved_key_t* key,
        const void* data,
        size_t size)
    {
        auto* capture = static_cast<CEndpointRecvCapture*>(user_data);
        if (capture == nullptr || key == nullptr) {
            return;
        }
        capture->called = true;
        capture->robot = key->robot;
        capture->channel_id = key->channel_id;
        capture->payload.resize(size);
        if (size > 0 && data != nullptr) {
            std::memcpy(capture->payload.data(), data, size);
        }
    }

#ifndef _WIN32
    template <typename T>
    T load_c_endpoint_symbol(const char* name) {
        return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
    }
#endif
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

TEST_F(EndpointTest, CEndpointInternalCacheSendRecvWorks) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_cache_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    hako_pdu_resolved_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "robot_c");
    key.channel_id = 42;

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "config/sample/endpoint_internal_cache.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_bool_t running = HAKO_PDU_FALSE;
    ASSERT_EQ(hako_pdu_endpoint_is_running(endpoint, &running), HAKO_PDU_ERR_OK);
    EXPECT_EQ(running, HAKO_PDU_TRUE);

    const std::uint8_t write_data[] = {0x10, 0x20, 0x30};
    ASSERT_EQ(
        hako_pdu_endpoint_send(endpoint, &key, write_data, sizeof(write_data)),
        HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    size_t received_size = 0;
    ASSERT_EQ(
        hako_pdu_endpoint_recv(endpoint, &key, recv_buf, sizeof(recv_buf), &received_size),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(received_size, sizeof(write_data));
    EXPECT_EQ(recv_buf[0], write_data[0]);
    EXPECT_EQ(recv_buf[1], write_data[1]);
    EXPECT_EQ(recv_buf[2], write_data[2]);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointStorageQueueRecvNextWorks) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_c_endpoint_storage_queue_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto out_comm_path = temp_base / "storage_queue_out_comm.json";
    const auto in_comm_path = temp_base / "storage_queue_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "c_storage_queue_out_endpoint"},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "c_storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    auto* writer = hako_pdu_endpoint_create("c_storage_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    auto* reader = hako_pdu_endpoint_create("c_storage_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_NE(writer, nullptr);
    ASSERT_NE(reader, nullptr);

    ASSERT_EQ(hako_pdu_endpoint_open(writer, out_endpoint_path.c_str()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(writer), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "RobotA");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "RobotB");
    key_a.channel_id = 10;
    key_b.channel_id = 20;

    const std::uint8_t payload_a1[] = {0x01};
    const std::uint8_t payload_b1[] = {0x0A};
    const std::uint8_t payload_a2[] = {0x02};
    ASSERT_EQ(hako_pdu_endpoint_send(writer, &key_a, payload_a1, sizeof(payload_a1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(writer, &key_b, payload_b1, sizeof(payload_b1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(writer, &key_a, payload_a2, sizeof(payload_a2)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_stop(writer), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(writer), HAKO_PDU_ERR_OK);

    ASSERT_EQ(hako_pdu_endpoint_open(reader, in_endpoint_path.c_str()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(reader), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(reader, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "RobotA");
    EXPECT_EQ(out_key.channel_id, 10U);
    EXPECT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x01});
    EXPECT_GE(out_timestamp_ns, 0U);

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(reader, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "RobotB");
    EXPECT_EQ(out_key.channel_id, 20U);
    EXPECT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x0A});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(reader, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "RobotA");
    EXPECT_EQ(out_key.channel_id, 10U);
    EXPECT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x02});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(reader, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(hako_pdu_endpoint_stop(reader), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(reader), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(writer);
    hako_pdu_endpoint_destroy(reader);
}

TEST_F(EndpointTest, CEndpointInternalLatestRecvNextReturnsPendingKeysInArrivalOrder) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_latest_recv_next_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "test/test_endpoint_buffer.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "robot_c_latest_a");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "robot_c_latest_b");
    key_a.channel_id = 71;
    key_b.channel_id = 72;

    const std::uint8_t payload_a1[] = {0x01};
    const std::uint8_t payload_b1[] = {0x02};
    const std::uint8_t payload_a2[] = {0x03};

    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a1, sizeof(payload_a1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_b, payload_b1, sizeof(payload_b1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a2, sizeof(payload_a2)), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_latest_a");
    EXPECT_EQ(out_key.channel_id, 71U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x03});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_latest_b");
    EXPECT_EQ(out_key.channel_id, 72U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x02});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointInternalQueueRecvNextReturnsGlobalArrivalOrder) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_queue_recv_next_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "test/test_endpoint_queue.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "robot_c_queue_a");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "robot_c_queue_b");
    key_a.channel_id = 73;
    key_b.channel_id = 74;

    const std::uint8_t payload_a1[] = {0x0A};
    const std::uint8_t payload_b1[] = {0x0B};
    const std::uint8_t payload_a2[] = {0x0C};

    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a1, sizeof(payload_a1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_b, payload_b1, sizeof(payload_b1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a2, sizeof(payload_a2)), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_queue_a");
    EXPECT_EQ(out_key.channel_id, 73U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x0A});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_queue_b");
    EXPECT_EQ(out_key.channel_id, 74U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x0B});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_queue_a");
    EXPECT_EQ(out_key.channel_id, 73U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x0C});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointRecvNextWorksWithoutRecvEventRegistration) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_recv_next_without_event_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "test/test_endpoint_queue.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "robot_c_no_event_a");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "robot_c_no_event_b");
    key_a.channel_id = 75;
    key_b.channel_id = 76;

    const std::uint8_t payload_a1[] = {0x21};
    const std::uint8_t payload_b1[] = {0x22};

    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a1, sizeof(payload_a1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_b, payload_b1, sizeof(payload_b1)), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_no_event_a");
    EXPECT_EQ(out_key.channel_id, 75U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x21});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_no_event_b");
    EXPECT_EQ(out_key.channel_id, 76U);
    ASSERT_EQ(received_size, 1U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x22});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointRecvWorksWithoutRecvEventRegistration) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_recv_without_event_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "test/test_endpoint_buffer.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "robot_c_recv_no_event");
    key.channel_id = 77;

    const std::uint8_t payload[] = {0x31, 0x32};
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key, payload, sizeof(payload)), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    size_t received_size = 0;
    ASSERT_EQ(
        hako_pdu_endpoint_recv(endpoint, &key, recv_buf, sizeof(recv_buf), &received_size),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(received_size, 2U);
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x31});
    EXPECT_EQ(recv_buf[1], std::uint8_t{0x32});

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointSetRecvEventSpecLatestPendingCountAndRecvNext) {
#ifdef _WIN32
    GTEST_SKIP() << "Dynamic symbol lookup for future C API spec is only enabled on POSIX test builds.";
#else
    using SetRecvEventFn = HakoPduErrorType (*)(hako_pdu_endpoint_handle_t*, const hako_pdu_resolved_key_t*);
    using GetPendingCountFn = HakoPduErrorType (*)(hako_pdu_endpoint_handle_t*, size_t*);

    const auto set_recv_event = load_c_endpoint_symbol<SetRecvEventFn>("hako_pdu_endpoint_set_recv_event");
    const auto get_pending_count = load_c_endpoint_symbol<GetPendingCountFn>("hako_pdu_endpoint_get_pending_count");
    if (set_recv_event == nullptr || get_pending_count == nullptr) {
        GTEST_SKIP() << "Future C receive-event APIs are not implemented yet.";
    }

    auto* endpoint = hako_pdu_endpoint_create("c_set_recv_event_latest_spec_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(hako_pdu_endpoint_open(endpoint, "test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "robot_c_set_event_latest_a");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "robot_c_set_event_latest_b");
    key_a.channel_id = 81;
    key_b.channel_id = 82;

    ASSERT_EQ(set_recv_event(endpoint, &key_a), HAKO_PDU_ERR_OK);
    ASSERT_EQ(set_recv_event(endpoint, &key_b), HAKO_PDU_ERR_OK);

    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, "\x01", 1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_b, "\x02", 1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, "\x03", 1), HAKO_PDU_ERR_OK);

    size_t pending_count = 0;
    ASSERT_EQ(get_pending_count(endpoint, &pending_count), HAKO_PDU_ERR_OK);
    EXPECT_EQ(pending_count, 2U);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_set_event_latest_a");
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x03});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_set_event_latest_b");
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x02});

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
#endif
}

TEST_F(EndpointTest, CEndpointSetRecvEventSpecQueuePendingCountAndRecvNext) {
#ifdef _WIN32
    GTEST_SKIP() << "Dynamic symbol lookup for future C API spec is only enabled on POSIX test builds.";
#else
    using SetRecvEventFn = HakoPduErrorType (*)(hako_pdu_endpoint_handle_t*, const hako_pdu_resolved_key_t*);
    using GetPendingCountFn = HakoPduErrorType (*)(hako_pdu_endpoint_handle_t*, size_t*);

    const auto set_recv_event = load_c_endpoint_symbol<SetRecvEventFn>("hako_pdu_endpoint_set_recv_event");
    const auto get_pending_count = load_c_endpoint_symbol<GetPendingCountFn>("hako_pdu_endpoint_get_pending_count");
    if (set_recv_event == nullptr || get_pending_count == nullptr) {
        GTEST_SKIP() << "Future C receive-event APIs are not implemented yet.";
    }

    auto* endpoint = hako_pdu_endpoint_create("c_set_recv_event_queue_spec_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(hako_pdu_endpoint_open(endpoint, "test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key_a{};
    hako_pdu_resolved_key_t key_b{};
    std::snprintf(key_a.robot, sizeof(key_a.robot), "%s", "robot_c_set_event_queue_a");
    std::snprintf(key_b.robot, sizeof(key_b.robot), "%s", "robot_c_set_event_queue_b");
    key_a.channel_id = 83;
    key_b.channel_id = 84;

    ASSERT_EQ(set_recv_event(endpoint, &key_a), HAKO_PDU_ERR_OK);
    ASSERT_EQ(set_recv_event(endpoint, &key_b), HAKO_PDU_ERR_OK);

    const std::uint8_t payload_a1[] = {0x41};
    const std::uint8_t payload_b1[] = {0x42};
    const std::uint8_t payload_a2[] = {0x43};

    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a1, sizeof(payload_a1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_b, payload_b1, sizeof(payload_b1)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key_a, payload_a2, sizeof(payload_a2)), HAKO_PDU_ERR_OK);

    size_t pending_count = 0;
    ASSERT_EQ(get_pending_count(endpoint, &pending_count), HAKO_PDU_ERR_OK);
    EXPECT_EQ(pending_count, 3U);

    std::uint8_t recv_buf[8] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_set_event_queue_a");
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x41});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_set_event_queue_b");
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x42});

    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(endpoint, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(out_key.robot, "robot_c_set_event_queue_a");
    EXPECT_EQ(recv_buf[0], std::uint8_t{0x43});

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
#endif
}

TEST_F(EndpointTest, CEndpointRecvReturnsNoSpaceWhenBufferTooSmall) {
    auto* endpoint = hako_pdu_endpoint_create("c_internal_cache_nospace_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    hako_pdu_resolved_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "robot_c");
    key.channel_id = 7;

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "config/sample/endpoint_internal_cache.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    const std::uint8_t write_data[] = {0x10, 0x20, 0x30};
    ASSERT_EQ(
        hako_pdu_endpoint_send(endpoint, &key, write_data, sizeof(write_data)),
        HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[2] = {};
    size_t received_size = 0;
    ASSERT_EQ(
        hako_pdu_endpoint_recv(endpoint, &key, recv_buf, sizeof(recv_buf), &received_size),
        HAKO_PDU_ERR_NO_SPACE);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointRecvNextReturnsNoSpaceWhenBufferTooSmall) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_c_endpoint_storage_queue_nospace_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto out_comm_path = temp_base / "storage_queue_out_comm.json";
    const auto in_comm_path = temp_base / "storage_queue_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "c_storage_queue_out_endpoint"},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "c_storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    auto* writer = hako_pdu_endpoint_create("c_storage_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    auto* reader = hako_pdu_endpoint_create("c_storage_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_NE(writer, nullptr);
    ASSERT_NE(reader, nullptr);

    ASSERT_EQ(hako_pdu_endpoint_open(writer, out_endpoint_path.c_str()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(writer), HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "RobotA");
    key.channel_id = 10;
    const std::uint8_t payload[] = {0x01, 0x02, 0x03};
    ASSERT_EQ(hako_pdu_endpoint_send(writer, &key, payload, sizeof(payload)), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_stop(writer), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(writer), HAKO_PDU_ERR_OK);

    ASSERT_EQ(hako_pdu_endpoint_open(reader, in_endpoint_path.c_str()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(reader), HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[2] = {};
    hako_pdu_resolved_key_t out_key{};
    std::uint64_t out_timestamp_ns = 0;
    size_t received_size = 0;
    ASSERT_EQ(
        hako_pdu_endpoint_recv_next(reader, recv_buf, sizeof(recv_buf), &out_key, &out_timestamp_ns, &received_size),
        HAKO_PDU_ERR_NO_SPACE);
    EXPECT_EQ(received_size, sizeof(payload));

    ASSERT_EQ(hako_pdu_endpoint_stop(reader), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(reader), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(writer);
    hako_pdu_endpoint_destroy(reader);
}

TEST_F(EndpointTest, CEndpointNameBasedApiWorks) {
    auto* endpoint = hako_pdu_endpoint_create("c_name_api_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "test/test_pdu_def_endpoint.json"),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    hako_pdu_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "TestRobot");
    std::snprintf(key.pdu, sizeof(key.pdu), "%s", "TestPDU");

    EXPECT_EQ(hako_pdu_endpoint_get_pdu_size(endpoint, &key), 8U);
    EXPECT_EQ(hako_pdu_endpoint_get_pdu_channel_id(endpoint, &key), 123);

    const std::uint8_t payload[] = {
        0xDE, 0xAD, 0xBE, 0xEF,
        0xCA, 0xFE, 0xBA, 0xBE
    };
    ASSERT_EQ(
        hako_pdu_endpoint_send_by_name(endpoint, &key, payload, sizeof(payload)),
        HAKO_PDU_ERR_OK);

    std::uint8_t recv_buf[8] = {};
    size_t received_size = 0;
    ASSERT_EQ(
        hako_pdu_endpoint_recv_by_name(endpoint, &key, recv_buf, sizeof(recv_buf), &received_size),
        HAKO_PDU_ERR_OK);
    ASSERT_EQ(received_size, sizeof(payload));
    EXPECT_EQ(std::memcmp(recv_buf, payload, sizeof(payload)), 0);

    hako_pdu_resolved_key_t resolved_key{};
    std::snprintf(resolved_key.robot, sizeof(resolved_key.robot), "%s", "TestRobot");
    resolved_key.channel_id = 123;
    char pdu_name[64] = {};
    ASSERT_EQ(
        hako_pdu_endpoint_get_pdu_name(endpoint, &resolved_key, pdu_name, sizeof(pdu_name)),
        HAKO_PDU_ERR_OK);
    EXPECT_STREQ(pdu_name, "TestPDU");

    hako_pdu_endpoint_process_recv_events(endpoint);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, CEndpointResolvedKeyCallbackWorks) {
    auto* endpoint = hako_pdu_endpoint_create("c_callback_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_NE(endpoint, nullptr);

    ASSERT_EQ(
        hako_pdu_endpoint_open(endpoint, "config/sample/endpoint_internal_cache.json"),
        HAKO_PDU_ERR_OK);

    hako_pdu_resolved_key_t key{};
    std::snprintf(key.robot, sizeof(key.robot), "%s", "robot_cb");
    key.channel_id = 9;

    CEndpointRecvCapture capture{};
    ASSERT_EQ(
        hako_pdu_endpoint_subscribe_on_recv_callback(endpoint, &key, c_endpoint_on_recv_capture, &capture),
        HAKO_PDU_ERR_OK);

    ASSERT_EQ(hako_pdu_endpoint_start(endpoint), HAKO_PDU_ERR_OK);

    const std::uint8_t payload[] = {0xAB, 0xCD};
    ASSERT_EQ(hako_pdu_endpoint_send(endpoint, &key, payload, sizeof(payload)), HAKO_PDU_ERR_OK);

    EXPECT_TRUE(capture.called);
    EXPECT_EQ(capture.robot, "robot_cb");
    EXPECT_EQ(capture.channel_id, 9U);
    ASSERT_EQ(capture.payload.size(), sizeof(payload));
    EXPECT_EQ(capture.payload[0], payload[0]);
    EXPECT_EQ(capture.payload[1], payload[1]);

    ASSERT_EQ(hako_pdu_endpoint_stop(endpoint), HAKO_PDU_ERR_OK);
    ASSERT_EQ(hako_pdu_endpoint_close(endpoint), HAKO_PDU_ERR_OK);
    hako_pdu_endpoint_destroy(endpoint);
}

TEST_F(EndpointTest, InternalLatestCallbackNotificationDoesNotConsumeReadableState) {
    hakoniwa::pdu::Endpoint endpoint("buffer_callback_state_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_cb_latest", 21);
    std::vector<std::vector<std::byte>> callbacks;
    endpoint.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    const std::vector<std::byte> payload1 = {std::byte{0x01}};
    const std::vector<std::byte> payload2 = {std::byte{0x02}, std::byte{0x03}};

    ASSERT_EQ(endpoint.send(key, payload1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, payload2), HAKO_PDU_ERR_OK);

    ASSERT_EQ(callbacks.size(), 2U);
    ASSERT_EQ(callbacks[0].size(), payload1.size());
    EXPECT_EQ(callbacks[0][0], payload1[0]);
    ASSERT_EQ(callbacks[1].size(), payload2.size());
    EXPECT_EQ(callbacks[1][0], payload2[0]);
    EXPECT_EQ(callbacks[1][1], payload2[1]);

    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, payload2.size());
    EXPECT_EQ(read_buffer[0], payload2[0]);
    EXPECT_EQ(read_buffer[1], payload2[1]);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, InternalQueueCallbackNotificationDoesNotConsumeQueuedEntries) {
    hakoniwa::pdu::Endpoint endpoint("queue_callback_state_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_cb_queue", 22);
    std::vector<std::vector<std::byte>> callbacks;
    endpoint.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    const std::vector<std::byte> payload1 = {std::byte{0x11}};
    const std::vector<std::byte> payload2 = {std::byte{0x22}};

    ASSERT_EQ(endpoint.send(key, payload1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, payload2), HAKO_PDU_ERR_OK);

    ASSERT_EQ(callbacks.size(), 2U);
    ASSERT_EQ(callbacks[0].size(), payload1.size());
    EXPECT_EQ(callbacks[0][0], payload1[0]);
    ASSERT_EQ(callbacks[1].size(), payload2.size());
    EXPECT_EQ(callbacks[1][0], payload2[0]);

    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;

    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, payload1.size());
    EXPECT_EQ(read_buffer[0], payload1[0]);

    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, payload2.size());
    EXPECT_EQ(read_buffer[0], payload2[0]);

    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, InternalBufferRecvNoEntryBeforeAnySend) {
    hakoniwa::pdu::Endpoint endpoint("buffer_no_recv_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_buffer_none", 31);
    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;
    EXPECT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, InternalBufferRecvWithCallbackAlwaysReturnsLatestValue) {
    hakoniwa::pdu::Endpoint endpoint("buffer_recv_counts_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_buffer_counts", 32);
    std::vector<std::vector<std::byte>> callbacks;
    endpoint.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    const std::vector<std::vector<std::byte>> sends = {
        {std::byte{0x01}},
        {std::byte{0x02}},
        {std::byte{0x03}}
    };
    for (const auto& payload : sends) {
        ASSERT_EQ(endpoint.send(key, payload), HAKO_PDU_ERR_OK);
    }

    ASSERT_EQ(callbacks.size(), sends.size());
    for (size_t i = 0; i < sends.size(); ++i) {
        ASSERT_EQ(callbacks[i].size(), sends[i].size());
        EXPECT_EQ(callbacks[i][0], sends[i][0]);
    }

    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;
    ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(read_len, sends.back().size());
    EXPECT_EQ(read_buffer[0], sends.back()[0]);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, InternalQueueRecvNoEntryBeforeAnySend) {
    hakoniwa::pdu::Endpoint endpoint("queue_no_recv_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_queue_none", 33);
    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;
    EXPECT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, InternalQueueRecvWithCallbackReturnsPerKeyFifoOrder) {
    hakoniwa::pdu::Endpoint endpoint("queue_recv_counts_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_queue_counts", 34);
    std::vector<std::vector<std::byte>> callbacks;
    endpoint.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    const std::vector<std::vector<std::byte>> sends = {
        {std::byte{0x11}},
        {std::byte{0x22}},
        {std::byte{0x33}}
    };
    for (const auto& payload : sends) {
        ASSERT_EQ(endpoint.send(key, payload), HAKO_PDU_ERR_OK);
    }

    ASSERT_EQ(callbacks.size(), sends.size());
    for (size_t i = 0; i < sends.size(); ++i) {
        ASSERT_EQ(callbacks[i].size(), sends[i].size());
        EXPECT_EQ(callbacks[i][0], sends[i][0]);
    }

    std::vector<std::byte> read_buffer(8);
    size_t read_len = 0;
    for (const auto& expected : sends) {
        ASSERT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_OK);
        ASSERT_EQ(read_len, expected.size());
        EXPECT_EQ(read_buffer[0], expected[0]);
    }
    EXPECT_EQ(endpoint.recv(key, read_buffer, read_len), HAKO_PDU_ERR_NO_ENTRY);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, RuntimeLatestRecvNextReturnsPendingKeysInArrivalOrder) {
    hakoniwa::pdu::Endpoint endpoint("runtime_latest_recv_next_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    const auto key_a = create_key("robot_runtime_latest_a", 51);
    const auto key_b = create_key("robot_runtime_latest_b", 52);

    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_b, std::vector<std::byte>{std::byte{0x02}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x03}}), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduRecord record{};

    // latest semantics:
    // - at most one pending entry per key
    // - recv_next returns keys in pending arrival order
    // - payload reflects the latest stored value for that key
    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_a.robot);
    EXPECT_EQ(record.key.channel_id, key_a.channel_id);
    ASSERT_EQ(record.payload.size(), 1U);
    EXPECT_EQ(record.payload[0], std::byte{0x03});

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_b.robot);
    EXPECT_EQ(record.key.channel_id, key_b.channel_id);
    ASSERT_EQ(record.payload.size(), 1U);
    EXPECT_EQ(record.payload[0], std::byte{0x02});

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, RuntimeLatestRecvByKeyConsumesPendingStateForThatKey) {
    hakoniwa::pdu::Endpoint endpoint("runtime_latest_recv_key_consumes_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_buffer.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    const auto key_a = create_key("robot_runtime_latest_key_a", 53);
    const auto key_b = create_key("robot_runtime_latest_key_b", 54);

    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x11}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_b, std::vector<std::byte>{std::byte{0x22}}), HAKO_PDU_ERR_OK);

    std::vector<std::byte> recv_buffer(8);
    size_t recv_size = 0;
    ASSERT_EQ(endpoint.recv(key_a, recv_buffer, recv_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(recv_size, 1U);
    EXPECT_EQ(recv_buffer[0], std::byte{0x11});

    hakoniwa::pdu::PduRecord record{};
    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_b.robot);
    EXPECT_EQ(record.key.channel_id, key_b.channel_id);
    ASSERT_EQ(record.payload.size(), 1U);
    EXPECT_EQ(record.payload[0], std::byte{0x22});

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, RuntimeQueueRecvNextReturnsGlobalArrivalOrder) {
    hakoniwa::pdu::Endpoint endpoint("runtime_queue_recv_next_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    const auto key_a = create_key("robot_runtime_queue_a", 61);
    const auto key_b = create_key("robot_runtime_queue_b", 62);

    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_b, std::vector<std::byte>{std::byte{0x02}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x03}}), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduRecord record{};

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_a.robot);
    EXPECT_EQ(record.key.channel_id, key_a.channel_id);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x01}}));

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_b.robot);
    EXPECT_EQ(record.key.channel_id, key_b.channel_id);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x02}}));

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_a.robot);
    EXPECT_EQ(record.key.channel_id, key_a.channel_id);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x03}}));

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, RuntimeQueueRecvByKeyConsumesMatchingArrivalEntry) {
    hakoniwa::pdu::Endpoint endpoint("runtime_queue_recv_key_consumes_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(endpoint.open("test/test_endpoint_queue.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    const auto key_a = create_key("robot_runtime_queue_key_a", 63);
    const auto key_b = create_key("robot_runtime_queue_key_b", 64);

    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x0A}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_b, std::vector<std::byte>{std::byte{0x0B}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key_a, std::vector<std::byte>{std::byte{0x0C}}), HAKO_PDU_ERR_OK);

    std::vector<std::byte> recv_buffer(8);
    size_t recv_size = 0;
    ASSERT_EQ(endpoint.recv(key_a, recv_buffer, recv_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(recv_size, 1U);
    EXPECT_EQ(recv_buffer[0], std::byte{0x0A});

    hakoniwa::pdu::PduRecord record{};
    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_b.robot);
    EXPECT_EQ(record.key.channel_id, key_b.channel_id);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x0B}}));

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, key_a.robot);
    EXPECT_EQ(record.key.channel_id, key_a.channel_id);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x0C}}));

    ASSERT_EQ(endpoint.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

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

TEST_F(EndpointTest, PduDefinitionCompactTest) {
    hakoniwa::pdu::Endpoint endpoint("pdu_def_compact_test", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(endpoint.open("test/test_pdu_def_endpoint_compact.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduKey key;
    key.robot = "TestRobot";
    key.pdu = "TestPDU";

    EXPECT_EQ(endpoint.get_pdu_size(key), 8);
    EXPECT_EQ(endpoint.get_pdu_channel_id(key), 123);

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

TEST_F(EndpointTest, NameResolverFileMapResolvesHostForAddressLookup) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_endpoint_name_resolver_test";
    fs::create_directories(temp_base);
    const auto map_path = temp_base / "node-ip-map.json";
    const auto comm_path = temp_base / "comm.json";

    {
        std::ofstream ofs(map_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"srv-01":"127.0.0.1"})";
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"protocol":"tcp","name_resolver":{"type":"file","path":"node-ip-map.json"}})";
    }

    nlohmann::json comm_json;
    {
        std::ifstream ifs(comm_path);
        ASSERT_TRUE(ifs.is_open());
        ifs >> comm_json;
    }
    hakoniwa::pdu::NameResolverConfig resolver{};
    std::string err;
    ASSERT_EQ(hakoniwa::pdu::load_name_resolver_config(comm_json, comm_path.string(), resolver, err), HAKO_PDU_ERR_OK)
        << err;
    ASSERT_TRUE(resolver.enabled);

    nlohmann::json endpoint_json = {
        {"address", "srv-01"},
        {"port", 64011}
    };
    addrinfo* resolved = nullptr;
    std::string resolved_address;
    ASSERT_EQ(hakoniwa::pdu::resolve_address(endpoint_json,
                                             SOCK_STREAM,
                                             &resolved,
                                             &resolver,
                                             &resolved_address),
              HAKO_PDU_ERR_OK);
    ASSERT_TRUE(resolved != nullptr);
    EXPECT_EQ(resolved_address, "127.0.0.1");
    freeaddrinfo(resolved);
}

TEST_F(EndpointTest, NameResolverStrictModeFailsOnUnknownHost) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_endpoint_name_resolver_strict_test";
    fs::create_directories(temp_base);
    const auto map_path = temp_base / "node-ip-map.json";
    const auto comm_path = temp_base / "comm.json";

    {
        std::ofstream ofs(map_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"srv-01":"127.0.0.1"})";
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"protocol":"tcp","name_resolver":{"type":"file","path":"node-ip-map.json","strict":true}})";
    }

    nlohmann::json comm_json;
    {
        std::ifstream ifs(comm_path);
        ASSERT_TRUE(ifs.is_open());
        ifs >> comm_json;
    }
    hakoniwa::pdu::NameResolverConfig resolver{};
    std::string err;
    ASSERT_EQ(hakoniwa::pdu::load_name_resolver_config(comm_json, comm_path.string(), resolver, err), HAKO_PDU_ERR_OK)
        << err;
    ASSERT_TRUE(resolver.strict);

    nlohmann::json endpoint_json = {
        {"address", "unknown-node"},
        {"port", 64011}
    };
    addrinfo* resolved = nullptr;
    EXPECT_EQ(hakoniwa::pdu::resolve_address(endpoint_json,
                                             SOCK_STREAM,
                                             &resolved,
                                             &resolver,
                                             nullptr),
              HAKO_PDU_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(resolved, nullptr);
}

TEST_F(EndpointTest, NameResolverUsesEnvPathWhenConfigOmitted) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_endpoint_name_resolver_env_test";
    fs::create_directories(temp_base);
    const auto map_path = temp_base / "node-ip-map.json";
    const auto comm_path = temp_base / "comm.json";

    {
        std::ofstream ofs(map_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"srv-01":"127.0.0.1"})";
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << R"({"protocol":"tcp"})";
    }

    ASSERT_EQ(setenv("HAKO_PDU_NAME_RESOLVER_PATH", map_path.c_str(), 1), 0);
    nlohmann::json comm_json;
    {
        std::ifstream ifs(comm_path);
        ASSERT_TRUE(ifs.is_open());
        ifs >> comm_json;
    }
    hakoniwa::pdu::NameResolverConfig resolver{};
    std::string err;
    ASSERT_EQ(hakoniwa::pdu::load_name_resolver_config(comm_json, comm_path.string(), resolver, err), HAKO_PDU_ERR_OK)
        << err;
    ASSERT_TRUE(resolver.enabled);

    nlohmann::json endpoint_json = {
        {"address", "srv-01"},
        {"port", 64011}
    };
    addrinfo* resolved = nullptr;
    ASSERT_EQ(hakoniwa::pdu::resolve_address(endpoint_json,
                                             SOCK_STREAM,
                                             &resolved,
                                             &resolver,
                                             nullptr),
              HAKO_PDU_ERR_OK);
    ASSERT_TRUE(resolved != nullptr);
    freeaddrinfo(resolved);
    unsetenv("HAKO_PDU_NAME_RESOLVER_PATH");
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

TEST_F(EndpointTest, TcpRecvNoEntryBeforeAnySendAndRecvNextUnsupported) {
    hakoniwa::pdu::Endpoint server("tcp_server_empty", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client_empty", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    const auto server_open_err = server.open("test/test_endpoint_tcp_server.json");
    if (server_open_err != HAKO_PDU_ERR_OK) {
        GTEST_SKIP() << "TCP server setup unavailable in this environment. err=" << static_cast<int>(server_open_err);
    }
    ASSERT_EQ(client.open("test/test_endpoint_tcp_client.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto key = create_key("robot_tcp_empty", 41);
    std::vector<std::byte> recv_buffer(8);
    size_t received_size = 0;
    EXPECT_EQ(server.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_UNSUPPORTED);
    EXPECT_EQ(client.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_UNSUPPORTED);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(server.recv_next(record), HAKO_PDU_ERR_UNSUPPORTED);
    EXPECT_EQ(client.recv_next(record), HAKO_PDU_ERR_UNSUPPORTED);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, TcpCallbackAndRecvRemainReadableAcrossThreeMessages) {
    hakoniwa::pdu::Endpoint server("tcp_server_cb", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client_cb", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    const auto server_open_err = server.open("test/test_endpoint_tcp_server.json");
    if (server_open_err != HAKO_PDU_ERR_OK) {
        GTEST_SKIP() << "TCP server setup unavailable in this environment. err=" << static_cast<int>(server_open_err);
    }
    ASSERT_EQ(client.open("test/test_endpoint_tcp_client.json"), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_tcp_cb", 42);
    std::vector<std::vector<std::byte>> callbacks;
    server.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::vector<std::vector<std::byte>> sends = {
        {std::byte{'a'}},
        {std::byte{'b'}},
        {std::byte{'c'}}
    };
    for (const auto& payload : sends) {
        ASSERT_EQ(client.send(key, payload), HAKO_PDU_ERR_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_EQ(callbacks.size(), sends.size());
    for (size_t i = 0; i < sends.size(); ++i) {
        ASSERT_EQ(callbacks[i].size(), sends[i].size());
        EXPECT_EQ(callbacks[i][0], sends[i][0]);
    }

    std::vector<std::byte> recv_buffer(8);
    size_t received_size = 0;
    for (const auto& expected : sends) {
        ASSERT_EQ(server.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_OK);
        ASSERT_EQ(received_size, expected.size());
        EXPECT_EQ(recv_buffer[0], expected[0]);
    }
    EXPECT_EQ(server.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_UNSUPPORTED);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(server.recv_next(record), HAKO_PDU_ERR_UNSUPPORTED);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, TcpCommunicationV1Test) {
    int server_port = find_available_port(SOCK_STREAM);
    ASSERT_GT(server_port, 0);

    hakoniwa::pdu::Endpoint server("tcp_server_v1", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client_v1", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(server.open("test/test_endpoint_tcp_server_v1.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_tcp_client_v1.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto key = create_key("robot_tcp_v1", 11);
    std::vector<std::byte> client_msg = {(std::byte)'v', (std::byte)'1', (std::byte)'p', (std::byte)'i', (std::byte)'n', (std::byte)'g'};

    ASSERT_EQ(client.send(key, client_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> server_buf(16);
    size_t server_len = 0;
    ASSERT_EQ(server.recv(key, server_buf, server_len), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server_len, client_msg.size());
    server_buf.resize(server_len);
    EXPECT_EQ(server_buf, client_msg);

    std::vector<std::byte> server_msg = {(std::byte)'v', (std::byte)'1', (std::byte)'p', (std::byte)'o', (std::byte)'n', (std::byte)'g'};
    ASSERT_EQ(server.send(key, server_msg), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> client_buf(16);
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

TEST_F(EndpointTest, TcpRecvCacheWriteDisabledStillDeliversCallbacks) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_tcp_recv_cache_write_disabled_test";
    const auto port = find_available_port(SOCK_STREAM);
    ASSERT_GT(port, 0);

    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto server_comm_path = temp_base / "tcp_server_comm.json";
    const auto client_comm_path = temp_base / "tcp_client_comm.json";
    const auto server_endpoint_path = temp_base / "endpoint_server.json";
    const auto client_endpoint_path = temp_base / "endpoint_client.json";
    const auto cache_path = (fs::current_path() / "config/sample/cache/queue.json").string();

    ASSERT_TRUE(create_dynamic_config(
        server_comm_path.string(),
        "config/sample/comm/tcp_server_inout_comm.json",
        port));
    ASSERT_TRUE(create_dynamic_config(
        client_comm_path.string(),
        "config/sample/comm/tcp_client_inout_comm.json",
        port,
        port));

    {
        std::ofstream ofs(server_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "tcp_server_no_recv_cache_write"},
            {"cache", cache_path},
            {"comm", server_comm_path.string()},
            {"recv_cache_write", false}
        }.dump(2);
    }
    {
        std::ofstream ofs(client_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "tcp_client_no_recv_cache_write"},
            {"cache", cache_path},
            {"comm", client_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint server("tcp_server_no_recv_cache_write", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client_no_recv_cache_write", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    const auto server_open_err = server.open(server_endpoint_path.string());
    if (server_open_err != HAKO_PDU_ERR_OK) {
        GTEST_SKIP() << "TCP server setup unavailable in this environment. err=" << static_cast<int>(server_open_err);
    }
    ASSERT_EQ(client.open(client_endpoint_path.string()), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_tcp_no_cache", 43);
    std::vector<std::vector<std::byte>> callbacks;
    server.subscribe_on_recv_callback(
        key,
        [&callbacks](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            callbacks.emplace_back(data.begin(), data.end());
        });

    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::vector<std::byte> payload = {std::byte{'x'}, std::byte{'y'}};
    ASSERT_EQ(client.send(key, payload), HAKO_PDU_ERR_OK);

    for (int i = 0; i < 20 && callbacks.empty(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ASSERT_EQ(callbacks.size(), 1U);
    ASSERT_EQ(callbacks[0].size(), payload.size());
    EXPECT_EQ(callbacks[0][0], payload[0]);
    EXPECT_EQ(callbacks[0][1], payload[1]);

    std::vector<std::byte> recv_buffer(8);
    size_t received_size = 0;
    EXPECT_EQ(server.recv(key, recv_buffer, received_size), HAKO_PDU_ERR_UNSUPPORTED);

    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);

    fs::remove_all(temp_base);
}

TEST_F(EndpointTest, TcpDisconnectedCallbackTest) {
    hakoniwa::pdu::Endpoint server("tcp_server_disconnect", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client("tcp_client_disconnect", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    std::atomic<bool> disconnected_called{false};
    std::atomic<int> reason_code{0};
    std::string reason_text;

    client.set_on_disconnected_callback(
        [&](const hakoniwa::pdu::DisconnectEvent& ev) {
            disconnected_called = true;
            reason_code = ev.reason_code;
            reason_text = ev.reason_text;
        });

    ASSERT_EQ(server.open("test/test_endpoint_tcp_server.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.open("test/test_endpoint_tcp_client.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.start(), HAKO_PDU_ERR_OK);

    // Give time for connect and steady state.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Force client-side disconnect detection by stopping server endpoint.
    ASSERT_EQ(server.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server.close(), HAKO_PDU_ERR_OK);

    bool notified = false;
    for (int i = 0; i < 40; ++i) {
        if (disconnected_called.load()) {
            notified = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    EXPECT_TRUE(notified);
    if (notified) {
        EXPECT_NE(reason_code.load(), HAKO_PDU_ERR_OK);
        EXPECT_FALSE(reason_text.empty());
    }

    ASSERT_EQ(client.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, TcpMuxTwoClientsTest) {
    hakoniwa::pdu::EndpointCommMultiplexer mux("tcp_mux", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    ASSERT_EQ(mux.open("test/mux/endpoint_tcp_mux.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(mux.start(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::Endpoint client1("tcp_mux_client1", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);
    hakoniwa::pdu::Endpoint client2("tcp_mux_client2", HAKO_PDU_ENDPOINT_DIRECTION_INOUT);

    ASSERT_EQ(client1.open("test/mux/endpoint_tcp_mux_client1.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client2.open("test/mux/endpoint_tcp_mux_client2.json"), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client1.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client2.start(), HAKO_PDU_ERR_OK);

    std::vector<std::unique_ptr<hakoniwa::pdu::Endpoint>> endpoints;
    for (int i = 0; i < 30 && endpoints.size() < 2; ++i) {
        auto batch = mux.take_endpoints();
        for (auto& ep : batch) {
            endpoints.push_back(std::move(ep));
        }
        if (endpoints.size() < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    ASSERT_EQ(endpoints.size(), 2u);

    auto key1 = create_key("robot_mux_1", 101);
    auto key2 = create_key("robot_mux_2", 102);
    std::vector<std::byte> msg1 = {(std::byte)'m', (std::byte)'u', (std::byte)'x', (std::byte)'1'};
    std::vector<std::byte> msg2 = {(std::byte)'m', (std::byte)'u', (std::byte)'x', (std::byte)'2'};

    ASSERT_EQ(client1.send(key1, msg1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client2.send(key2, msg2), HAKO_PDU_ERR_OK);

    auto find_endpoint = [&](const hakoniwa::pdu::PduResolvedKey& key,
                             const std::vector<std::byte>& expected) -> int {
        for (int attempt = 0; attempt < 20; ++attempt) {
            for (size_t i = 0; i < endpoints.size(); ++i) {
                std::vector<std::byte> buf(64);
                size_t len = 0;
                auto err = endpoints[i]->recv(key, buf, len);
                if (err == HAKO_PDU_ERR_OK) {
                    buf.resize(len);
                    if (buf == expected) {
                        return static_cast<int>(i);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return -1;
    };

    int idx1 = find_endpoint(key1, msg1);
    int idx2 = find_endpoint(key2, msg2);
    ASSERT_GE(idx1, 0);
    ASSERT_GE(idx2, 0);
    ASSERT_NE(idx1, idx2);

    std::vector<std::byte> resp1 = {(std::byte)'r', (std::byte)'1'};
    std::vector<std::byte> resp2 = {(std::byte)'r', (std::byte)'2'};
    ASSERT_EQ(endpoints[static_cast<size_t>(idx1)]->send(key1, resp1), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoints[static_cast<size_t>(idx2)]->send(key2, resp2), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<std::byte> client_buf(8);
    size_t client_len = 0;
    ASSERT_EQ(client1.recv(key1, client_buf, client_len), HAKO_PDU_ERR_OK);
    client_buf.resize(client_len);
    EXPECT_EQ(client_buf, resp1);

    client_buf.assign(8, std::byte{0});
    client_len = 0;
    ASSERT_EQ(client2.recv(key2, client_buf, client_len), HAKO_PDU_ERR_OK);
    client_buf.resize(client_len);
    EXPECT_EQ(client_buf, resp2);

    for (auto& ep : endpoints) {
        ASSERT_EQ(ep->stop(), HAKO_PDU_ERR_OK);
        ASSERT_EQ(ep->close(), HAKO_PDU_ERR_OK);
    }

    ASSERT_EQ(client1.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client2.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client1.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client2.close(), HAKO_PDU_ERR_OK);

    ASSERT_EQ(mux.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(mux.close(), HAKO_PDU_ERR_OK);
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

TEST_F(EndpointTest, StorageCommQueueModePersistsAllSends) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto comm_path = temp_base / "storage_queue_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "storage_queue.bin";

    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", output_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_endpoint"},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_queue_test", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("robot_storage", 10);
    ASSERT_EQ(endpoint.send(key, std::vector<std::byte>{std::byte{0xAA}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, std::vector<std::byte>{std::byte{0xBB}, std::byte{0xCC}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const auto packets = read_storage_packets(output_path);
    ASSERT_EQ(packets.size(), 2U);
    auto decoded0 = hakoniwa::pdu::comm::DataPacket::decode(packets[0], "v2");
    auto decoded1 = hakoniwa::pdu::comm::DataPacket::decode(packets[1], "v2");
    ASSERT_TRUE(decoded0);
    ASSERT_TRUE(decoded1);
    EXPECT_EQ(decoded0->get_robot_name(), "robot_storage");
    EXPECT_EQ(decoded0->get_channel_id(), 10U);
    EXPECT_EQ(decoded0->get_pdu_data(), (std::vector<std::byte>{std::byte{0xAA}}));
    EXPECT_EQ(decoded1->get_pdu_data(), (std::vector<std::byte>{std::byte{0xBB}, std::byte{0xCC}}));
}

TEST_F(EndpointTest, StorageCommQueueOpenValidatesExistingFile) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_open_validate_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto comm_path = temp_base / "storage_queue_in_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto storage_path = temp_base / "storage_queue.bin";

    {
        hakoniwa::pdu::comm::DataPacket packet("robot_storage", 10U, std::vector<std::byte>{std::byte{0x11}});
        const auto encoded = packet.encode("v2");
        std::fstream ofs(storage_path, std::ios::binary | std::ios::out | std::ios::trunc);
        ASSERT_TRUE(ofs.is_open());
        ofs.seekp(sizeof(StorageHeaderV1), std::ios::beg);
        const std::uint32_t size = static_cast<std::uint32_t>(encoded.size());
        const char len_buf[4] = {
            static_cast<char>(size & 0xFFu),
            static_cast<char>((size >> 8) & 0xFFu),
            static_cast<char>((size >> 16) & 0xFFu),
            static_cast<char>((size >> 24) & 0xFFu)
        };
        ofs.write(len_buf, sizeof(len_buf));
        ofs.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
        ofs.flush();
        const auto end_pos = ofs.tellp();
        ASSERT_GE(end_pos, 0);
        write_queue_storage_header(ofs, 2, static_cast<std::uint64_t>(end_pos));
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_queue_in_test", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, StorageCommQueueInOpenFailsWhenFrameIsTruncated) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_open_fail_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto comm_path = temp_base / "storage_queue_in_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto storage_path = temp_base / "storage_queue_corrupted.bin";

    {
        std::fstream ofs(storage_path, std::ios::binary | std::ios::out | std::ios::trunc);
        ASSERT_TRUE(ofs.is_open());
        ofs.seekp(sizeof(StorageHeaderV1), std::ios::beg);
        const char len_buf[4] = {0x08, 0x00, 0x00, 0x00};
        const char payload[3] = {0x01, 0x02, 0x03};
        ofs.write(len_buf, sizeof(len_buf));
        ofs.write(payload, sizeof(payload));
        ofs.flush();
        const auto end_pos = ofs.tellp();
        ASSERT_GE(end_pos, 0);
        write_queue_storage_header(ofs, 2, static_cast<std::uint64_t>(end_pos));
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_queue_in_fail_test", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_INVALID_CONFIG);
}

TEST_F(EndpointTest, StorageCommQueueInRecvFiltersByKeyInOrder) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_recv_filter_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto out_comm_path = temp_base / "storage_queue_out_comm.json";
    const auto in_comm_path = temp_base / "storage_queue_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_out_endpoint"},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint writer("storage_queue_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(writer.open(out_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotB", 20), std::vector<std::byte>{std::byte{0x0A}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x02}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.close(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::Endpoint reader("storage_queue_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(reader.open(in_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.start(), HAKO_PDU_ERR_OK);

    std::vector<std::byte> recv_buf(4);
    size_t recv_size = 0;
    ASSERT_EQ(reader.recv(create_key("RobotA", 10), recv_buf, recv_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(recv_size, 1U);
    EXPECT_EQ(recv_buf[0], std::byte{0x01});

    ASSERT_EQ(reader.recv(create_key("RobotA", 10), recv_buf, recv_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(recv_size, 1U);
    EXPECT_EQ(recv_buf[0], std::byte{0x02});

    ASSERT_EQ(reader.recv(create_key("RobotA", 10), recv_buf, recv_size), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(reader.recv(create_key("RobotB", 20), recv_buf, recv_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(recv_size, 1U);
    EXPECT_EQ(recv_buf[0], std::byte{0x0A});

    ASSERT_EQ(reader.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, StorageCommQueueRecvNextReturnsGlobalLogOrder) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_recv_next_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto out_comm_path = temp_base / "storage_queue_out_comm.json";
    const auto in_comm_path = temp_base / "storage_queue_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_out_endpoint"},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_in_endpoint"},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint writer("storage_queue_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(writer.open(out_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotB", 20), std::vector<std::byte>{std::byte{0x02}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x03}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.close(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::Endpoint reader("storage_queue_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(reader.open(in_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.start(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduRecord record{};
    ASSERT_EQ(reader.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, "RobotA");
    EXPECT_EQ(record.key.channel_id, 10);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x01}}));

    ASSERT_EQ(reader.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, "RobotB");
    EXPECT_EQ(record.key.channel_id, 20);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x02}}));

    ASSERT_EQ(reader.recv_next(record), HAKO_PDU_ERR_OK);
    EXPECT_EQ(record.key.robot, "RobotA");
    EXPECT_EQ(record.key.channel_id, 10);
    EXPECT_EQ(record.payload, (std::vector<std::byte>{std::byte{0x03}}));

    ASSERT_EQ(reader.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);
    ASSERT_EQ(reader.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, StorageCommQueueRecvNextReturnsNoEntryBeforeAnySend) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_queue_recv_next_empty_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto in_comm_path = temp_base / "storage_queue_in_comm.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::fstream ofs(storage_path, std::ios::binary | std::ios::out | std::ios::trunc);
        ASSERT_TRUE(ofs.is_open());
        write_queue_storage_header(ofs, 2, sizeof(StorageHeaderV1));
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_empty_reader"},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint reader("storage_queue_empty_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(reader.open(in_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.start(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::PduRecord record{};
    EXPECT_EQ(reader.recv_next(record), HAKO_PDU_ERR_NO_ENTRY);

    ASSERT_EQ(reader.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, StorageCommLatestModeKeepsOnlyLatestPerChannel) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_latest_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    const auto pdu_def_path = (fs::current_path() / "test/test_pdudef_compact.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto comm_path = temp_base / "storage_latest_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "storage_latest.bin";

    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", output_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_latest_test", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);

    auto key = create_key("TestRobot", 123);
    ASSERT_EQ(endpoint.send(key, std::vector<std::byte>{
        std::byte{0x11}, std::byte{0x11}, std::byte{0x11}, std::byte{0x11},
        std::byte{0x11}, std::byte{0x11}, std::byte{0x11}, std::byte{0x11}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(key, std::vector<std::byte>{
        std::byte{0x22}, std::byte{0x22}, std::byte{0x22}, std::byte{0x22},
        std::byte{0x33}, std::byte{0x33}, std::byte{0x33}, std::byte{0x33}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const auto packet = read_latest_packet(output_path, 0);
    ASSERT_FALSE(packet.empty());
    auto decoded = hakoniwa::pdu::comm::DataPacket::decode(packet, "v2");
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->get_robot_name(), "TestRobot");
    EXPECT_EQ(decoded->get_channel_id(), 123U);
    EXPECT_EQ(decoded->get_pdu_data(), (std::vector<std::byte>{
        std::byte{0x22}, std::byte{0x22}, std::byte{0x22}, std::byte{0x22},
        std::byte{0x33}, std::byte{0x33}, std::byte{0x33}, std::byte{0x33}}));
}

TEST_F(EndpointTest, StorageCommLatestModeReadsTwoKeysFromValidatedFile) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_latest_recv_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto pdutypes_path = temp_base / "pdutypes.json";
    const auto pdudef_path = temp_base / "pdudef.json";
    const auto storage_path = temp_base / "storage_latest.bin";
    const auto out_comm_path = temp_base / "storage_latest_out_comm.json";
    const auto in_comm_path = temp_base / "storage_latest_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(pdutypes_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json::array({
            {{"channel_id", 10}, {"pdu_size", 4}, {"name", "pdu_a"}, {"type", "test_msgs/PduA"}},
            {{"channel_id", 20}, {"pdu_size", 4}, {"name", "pdu_b"}, {"type", "test_msgs/PduB"}}
        }).dump(2);
    }
    {
        std::ofstream ofs(pdudef_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"paths", nlohmann::json::array({{{"id", "default"}, {"path", pdutypes_path.string()}}})},
            {"robots", nlohmann::json::array({{{"name", "RobotA"}, {"pdutypes_id", "default"}}})}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_out_endpoint"},
            {"pdu_def_path", pdudef_path.string()},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_in_endpoint"},
            {"pdu_def_path", pdudef_path.string()},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint writer("storage_latest_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(writer.open(out_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.send(create_key("RobotA", 20), std::vector<std::byte>{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.close(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::Endpoint reader("storage_latest_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(reader.open(in_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.start(), HAKO_PDU_ERR_OK);

    std::vector<std::byte> recv_a(4);
    std::vector<std::byte> recv_b(4);
    size_t recv_a_size = 0;
    size_t recv_b_size = 0;
    ASSERT_EQ(reader.recv(create_key("RobotA", 10), recv_a, recv_a_size), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.recv(create_key("RobotA", 20), recv_b, recv_b_size), HAKO_PDU_ERR_OK);
    EXPECT_EQ(recv_a_size, 4U);
    EXPECT_EQ(recv_b_size, 4U);
    EXPECT_EQ(recv_a, (std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}));
    EXPECT_EQ(recv_b, (std::vector<std::byte>{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D}}));
    ASSERT_EQ(reader.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(reader.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, StorageCommLatestInOpenFailsWhenEntriesAreUninitialized) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_latest_in_fail_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    const auto pdu_def_path = (fs::current_path() / "test/test_pdudef_compact.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_latest.bin";
    const auto out_comm_path = temp_base / "storage_latest_out_comm.json";
    const auto in_comm_path = temp_base / "storage_latest_in_comm.json";
    const auto out_endpoint_path = temp_base / "endpoint_out.json";
    const auto in_endpoint_path = temp_base / "endpoint_in.json";

    {
        std::ofstream ofs(out_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "in"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(out_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_out_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", out_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(in_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_in_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", in_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint writer("storage_latest_writer", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(writer.open(out_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(writer.close(), HAKO_PDU_ERR_OK);

    hakoniwa::pdu::Endpoint reader("storage_latest_reader", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    ASSERT_EQ(reader.open(in_endpoint_path.string()), HAKO_PDU_ERR_NO_ENTRY);
}

#ifdef HAKO_PDU_ENDPOINT_HAS_ZENOH
TEST_F(EndpointTest, ZenohCommPeerToPeerPubSubDeliversPayloadToCallback) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_zenoh_pubsub_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    const auto pdu_def_path = (fs::current_path() / "config/sample/comm/storage_example/pdudef.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base / "zenoh");

    const int port = find_available_port(SOCK_STREAM);
    if (port <= 0) {
        GTEST_SKIP() << "No bindable TCP port available in this environment";
    }

    const auto peer_listen_path = temp_base / "zenoh" / "peer_listen.json5";
    const auto peer_connect_path = temp_base / "zenoh" / "peer_connect.json5";
    const auto sub_comm_path = temp_base / "zenoh_sub_comm.json";
    const auto pub_comm_path = temp_base / "zenoh_pub_comm.json";
    const auto sub_endpoint_path = temp_base / "endpoint_zenoh_sub.json";
    const auto pub_endpoint_path = temp_base / "endpoint_zenoh_pub.json";

    {
        std::ofstream ofs(peer_listen_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "{\n"
               "  mode: \"peer\",\n"
               "  listen: {\n"
               "    endpoints: [\n"
            << "      \"tcp/127.0.0.1:" << port << "\"\n"
               "    ]\n"
               "  }\n"
               "}\n";
    }
    {
        std::ofstream ofs(peer_connect_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "{\n"
               "  mode: \"peer\",\n"
               "  connect: {\n"
               "    endpoints: [\n"
            << "      \"tcp/127.0.0.1:" << port << "\"\n"
               "    ]\n"
               "  }\n"
               "}\n";
    }
    {
        std::ofstream ofs(sub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "zenoh"},
            {"name", "zenoh_sub"},
            {"direction", "in"},
            {"zenoh", {
                {"config_path", "zenoh/peer_listen.json5"},
                {"key_prefix", "hakoniwa"},
                {"io", {
                    {"robots", nlohmann::json::array({
                        {
                            {"name", "StorageDemo"},
                            {"pdu", nlohmann::json::array({
                                {
                                    {"name", "sample_state"},
                                    {"notify_on_recv", true}
                                }
                            })}
                        }
                    })}
                }}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(pub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "zenoh"},
            {"name", "zenoh_pub"},
            {"direction", "out"},
            {"zenoh", {
                {"config_path", "zenoh/peer_connect.json5"},
                {"key_prefix", "hakoniwa"},
                {"io", {
                    {"robots", nlohmann::json::array({
                        {
                            {"name", "StorageDemo"},
                            {"pdu", nlohmann::json::array({
                                {
                                    {"name", "sample_state"},
                                    {"notify_on_recv", false}
                                }
                            })}
                        }
                    })}
                }}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(sub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_zenoh_sub_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", sub_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(pub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_zenoh_pub_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", pub_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint subscriber("zenoh_sub_test", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::Endpoint publisher("zenoh_pub_test", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(subscriber.open(sub_endpoint_path.string()), HAKO_PDU_ERR_OK);

    std::atomic<bool> received{false};
    std::atomic<std::uint64_t> received_value{0};
    subscriber.subscribe_on_recv_callback(
        hakoniwa::pdu::PduResolvedKey{"StorageDemo", 0},
        [&received, &received_value](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            if (data.size() == sizeof(std::uint64_t)) {
                std::uint64_t value = 0;
                std::memcpy(&value, data.data(), sizeof(value));
                received_value.store(value);
                received.store(true);
            }
        });
    ASSERT_EQ(subscriber.start(), HAKO_PDU_ERR_OK);

    ASSERT_EQ(publisher.open(pub_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto key = create_key("StorageDemo", 0);
    const std::uint64_t expected = 42;
    std::vector<std::byte> payload(sizeof(expected));
    std::memcpy(payload.data(), &expected, sizeof(expected));
    ASSERT_EQ(publisher.send(key, payload), HAKO_PDU_ERR_OK);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(received.load());
    EXPECT_EQ(received_value.load(), expected);

    ASSERT_EQ(publisher.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.close(), HAKO_PDU_ERR_OK);
}

TEST_F(EndpointTest, RmwZenohCommPeerToPeerPubSubDeliversOpaquePayloadToCallback) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_rmw_zenoh_pubsub_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    const auto pdu_def_path = (fs::current_path() / "config/sample/comm/storage_example/pdudef.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base / "zenoh");

    const int port = find_available_port(SOCK_STREAM);
    if (port <= 0) {
        GTEST_SKIP() << "No bindable TCP port available in this environment";
    }

    const auto peer_listen_path = temp_base / "zenoh" / "peer_listen.json5";
    const auto peer_connect_path = temp_base / "zenoh" / "peer_connect.json5";
    const auto sub_comm_path = temp_base / "rmw_zenoh_sub_comm.json";
    const auto pub_comm_path = temp_base / "rmw_zenoh_pub_comm.json";
    const auto sub_endpoint_path = temp_base / "endpoint_rmw_zenoh_sub.json";
    const auto pub_endpoint_path = temp_base / "endpoint_rmw_zenoh_pub.json";

    {
        std::ofstream ofs(peer_listen_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "{\n"
               "  mode: \"peer\",\n"
               "  listen: {\n"
               "    endpoints: [\n"
            << "      \"tcp/127.0.0.1:" << port << "\"\n"
               "    ]\n"
               "  }\n"
               "}\n";
    }
    {
        std::ofstream ofs(peer_connect_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "{\n"
               "  mode: \"peer\",\n"
               "  connect: {\n"
               "    endpoints: [\n"
            << "      \"tcp/127.0.0.1:" << port << "\"\n"
               "    ]\n"
               "  }\n"
               "}\n";
    }

    const auto make_rmw_zenoh_comm = [](const std::string& direction, const std::string& config_path) {
        return nlohmann::json{
            {"protocol", "rmw_zenoh"},
            {"name", "rmw_zenoh_" + direction},
            {"direction", direction},
            {"rmw_zenoh", {
                {"config_path", config_path},
                {"domain_id", 0},
                {"timestamp", {
                    {"source", "system_clock"}
                }},
                {"mappings", nlohmann::json::array({
                    {
                        {"endpoint", {
                            {"robot", "StorageDemo"},
                            {"pdu", "sample_state"},
                            {"notify_on_recv", true}
                        }},
                        {"ros2", {
                            {"topic", "/sample_state"},
                            {"type", "auto"},
                            {"type_hash", "RIHS01_TEST_HASH"},
                            {"gid", "auto"}
                        }}
                    }
                })}
            }}
        };
    };

    {
        std::ofstream ofs(sub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << make_rmw_zenoh_comm("in", "zenoh/peer_listen.json5").dump(2);
    }
    {
        std::ofstream ofs(pub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << make_rmw_zenoh_comm("out", "zenoh/peer_connect.json5").dump(2);
    }
    {
        std::ofstream ofs(sub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_rmw_zenoh_sub_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", sub_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(pub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_rmw_zenoh_pub_endpoint"},
            {"pdu_def_path", pdu_def_path},
            {"cache", cache_path},
            {"comm", pub_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint subscriber("rmw_zenoh_sub_test", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::Endpoint publisher("rmw_zenoh_pub_test", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(subscriber.open(sub_endpoint_path.string()), HAKO_PDU_ERR_OK);

    std::atomic<bool> received{false};
    std::vector<std::byte> received_payload;
    subscriber.subscribe_on_recv_callback(
        hakoniwa::pdu::PduResolvedKey{"StorageDemo", 0},
        [&received, &received_payload](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            received_payload.assign(data.begin(), data.end());
            received.store(true);
        });
    ASSERT_EQ(subscriber.start(), HAKO_PDU_ERR_OK);

    ASSERT_EQ(publisher.open(pub_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto key = create_key("StorageDemo", 0);
    const std::vector<std::byte> payload{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08}
    };
    ASSERT_EQ(publisher.send(key, payload), HAKO_PDU_ERR_OK);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(received.load());
    EXPECT_EQ(received_payload, payload);

    ASSERT_EQ(publisher.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.close(), HAKO_PDU_ERR_OK);
}
#endif

#ifdef HAKO_PDU_ENDPOINT_HAS_MQTT
TEST_F(EndpointTest, MqttCommPubSubDeliversPayloadToCallback) {
    namespace fs = std::filesystem;
    const auto mosquitto_path = find_executable_on_path("mosquitto");
    if (mosquitto_path.empty()) {
        GTEST_SKIP() << "mosquitto broker executable not found in PATH";
    }

    const int port = find_available_port(SOCK_STREAM);
    if (port <= 0) {
        GTEST_SKIP() << "No bindable TCP port available in this environment";
    }

    const auto temp_base = fs::temp_directory_path() / "hako_pdu_mqtt_pubsub_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto broker_conf_path = temp_base / "mosquitto.conf";
    const auto sub_comm_path = temp_base / "mqtt_sub_comm.json";
    const auto pub_comm_path = temp_base / "mqtt_pub_comm.json";
    const auto sub_endpoint_path = temp_base / "endpoint_mqtt_sub.json";
    const auto pub_endpoint_path = temp_base / "endpoint_mqtt_pub.json";

    {
        std::ofstream ofs(broker_conf_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << "listener " << port << " 127.0.0.1\n"
               "allow_anonymous true\n"
               "persistence false\n";
    }

    const pid_t broker_pid = launch_mosquitto(mosquitto_path, broker_conf_path);
    ASSERT_GT(broker_pid, 0);

    auto cleanup = [&broker_pid]() { stop_child_process(broker_pid); };

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    {
        std::ofstream ofs(sub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "mqtt"},
            {"name", "mqtt_sub"},
            {"direction", "in"},
            {"mqtt", {
                {"broker", "tcp://127.0.0.1:" + std::to_string(port)},
                {"client_id", "hakoniwa_mqtt_test_sub"},
                {"topic_prefix", "hakoniwa"},
                {"qos", 0},
                {"retain", false}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(pub_comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "mqtt"},
            {"name", "mqtt_pub"},
            {"direction", "out"},
            {"mqtt", {
                {"broker", "tcp://127.0.0.1:" + std::to_string(port)},
                {"client_id", "hakoniwa_mqtt_test_pub"},
                {"topic_prefix", "hakoniwa"},
                {"qos", 0},
                {"retain", false}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(sub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_mqtt_sub_endpoint"},
            {"cache", cache_path},
            {"comm", sub_comm_path.string()}
        }.dump(2);
    }
    {
        std::ofstream ofs(pub_endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "sample_mqtt_pub_endpoint"},
            {"cache", cache_path},
            {"comm", pub_comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint subscriber("mqtt_sub_test", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::Endpoint publisher("mqtt_pub_test", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    ASSERT_EQ(subscriber.open(sub_endpoint_path.string()), HAKO_PDU_ERR_OK);

    std::atomic<bool> received{false};
    std::atomic<std::uint64_t> received_value{0};
    subscriber.subscribe_on_recv_callback(
        hakoniwa::pdu::PduResolvedKey{"StorageDemo", 0},
        [&received, &received_value](const hakoniwa::pdu::PduResolvedKey&, std::span<const std::byte> data) {
            if (data.size() == sizeof(std::uint64_t)) {
                std::uint64_t value = 0;
                std::memcpy(&value, data.data(), sizeof(value));
                received_value.store(value);
                received.store(true);
            }
        });
    ASSERT_EQ(subscriber.start(), HAKO_PDU_ERR_OK);

    ASSERT_EQ(publisher.open(pub_endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.start(), HAKO_PDU_ERR_OK);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    const auto key = create_key("StorageDemo", 0);
    const std::uint64_t expected = 42;
    std::vector<std::byte> payload(sizeof(expected));
    std::memcpy(payload.data(), &expected, sizeof(expected));
    ASSERT_EQ(publisher.send(key, payload), HAKO_PDU_ERR_OK);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!received.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    EXPECT_TRUE(received.load());
    EXPECT_EQ(received_value.load(), expected);

    ASSERT_EQ(publisher.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(publisher.close(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(subscriber.close(), HAKO_PDU_ERR_OK);
    cleanup();
}
#endif

#ifdef HAKO_PDU_STORAGE_DEBUG_EXE
TEST_F(EndpointTest, StorageDebugToolPrintsQueueSummaryInLogOrder) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_debug_queue_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto comm_path = temp_base / "storage_queue_out_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "queue_debug.txt";

    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_debug_endpoint"},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_queue_debug", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotB", 20), std::vector<std::byte>{std::byte{0x02}, std::byte{0x03}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const std::string command = std::string(HAKO_PDU_STORAGE_DEBUG_EXE)
        + " \"" + storage_path.string() + "\" > \"" + output_path.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const std::string output = read_text_file(output_path);
    EXPECT_NE(output.find("mode: queue"), std::string::npos);
    EXPECT_NE(output.find("key=RobotA/10"), std::string::npos);
    EXPECT_NE(output.find("key=RobotB/20"), std::string::npos);
    EXPECT_LT(output.find("key=RobotA/10"), output.find("key=RobotB/20"));
}

TEST_F(EndpointTest, StorageDebugToolPrintsQueueJsonSummary) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_debug_queue_json_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto storage_path = temp_base / "storage_queue.bin";
    const auto comm_path = temp_base / "storage_queue_out_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "queue_debug.json";

    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "queue"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_queue_debug_endpoint"},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_queue_debug_json", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotB", 20), std::vector<std::byte>{std::byte{0x02}, std::byte{0x03}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const std::string command = std::string(HAKO_PDU_STORAGE_DEBUG_EXE)
        + " \"" + storage_path.string() + "\" --json > \"" + output_path.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);

    nlohmann::json output = nlohmann::json::parse(read_text_file(output_path));
    EXPECT_EQ(output.at("mode").get<std::string>(), "queue");
    ASSERT_EQ(output.at("records").size(), 2U);
    EXPECT_EQ(output.at("records").at(0).at("key").at("robot_name").get<std::string>(), "RobotA");
    EXPECT_EQ(output.at("records").at(0).at("key").at("channel_id").get<int>(), 10);
    EXPECT_EQ(output.at("records").at(1).at("key").at("robot_name").get<std::string>(), "RobotB");
    EXPECT_TRUE(output.at("records").at(0).at("storage_timestamp_ns").is_null());
    EXPECT_TRUE(output.at("records").at(0).contains("packet_offset"));
}

TEST_F(EndpointTest, StorageDebugToolPrintsLatestEntrySummary) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_debug_latest_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto pdutypes_path = temp_base / "pdutypes.json";
    const auto pdudef_path = temp_base / "pdudef.json";
    const auto storage_path = temp_base / "storage_latest.bin";
    const auto comm_path = temp_base / "storage_latest_out_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "latest_debug.txt";

    {
        std::ofstream ofs(pdutypes_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json::array({
            {{"channel_id", 10}, {"pdu_size", 4}, {"name", "pdu_a"}, {"type", "test_msgs/PduA"}},
            {{"channel_id", 20}, {"pdu_size", 4}, {"name", "pdu_b"}, {"type", "test_msgs/PduB"}}
        }).dump(2);
    }
    {
        std::ofstream ofs(pdudef_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"paths", nlohmann::json::array({{{"id", "default"}, {"path", pdutypes_path.string()}}})},
            {"robots", nlohmann::json::array({{{"name", "RobotA"}, {"pdutypes_id", "default"}}})}
        }.dump(2);
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_debug_endpoint"},
            {"pdu_def_path", pdudef_path.string()},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_latest_debug", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 20), std::vector<std::byte>{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const std::string command = std::string(HAKO_PDU_STORAGE_DEBUG_EXE)
        + " \"" + storage_path.string() + "\" > \"" + output_path.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const std::string output = read_text_file(output_path);
    EXPECT_NE(output.find("mode: latest"), std::string::npos);
    EXPECT_NE(output.find("key=RobotA/10"), std::string::npos);
    EXPECT_NE(output.find("key=RobotA/20"), std::string::npos);
    EXPECT_NE(output.find("initialized=yes"), std::string::npos);
    EXPECT_NE(output.find("storage_timestamp_ns="), std::string::npos);
}

TEST_F(EndpointTest, StorageDebugToolPrintsLatestJsonSummary) {
    namespace fs = std::filesystem;
    const auto temp_base = fs::temp_directory_path() / "hako_pdu_storage_debug_latest_json_test";
    const auto cache_path = (fs::current_path() / "config/sample/cache/buffer.json").string();
    fs::remove_all(temp_base);
    fs::create_directories(temp_base);

    const auto pdutypes_path = temp_base / "pdutypes.json";
    const auto pdudef_path = temp_base / "pdudef.json";
    const auto storage_path = temp_base / "storage_latest.bin";
    const auto comm_path = temp_base / "storage_latest_out_comm.json";
    const auto endpoint_path = temp_base / "endpoint.json";
    const auto output_path = temp_base / "latest_debug.json";

    {
        std::ofstream ofs(pdutypes_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json::array({
            {{"channel_id", 10}, {"pdu_size", 4}, {"name", "pdu_a"}, {"type", "test_msgs/PduA"}},
            {{"channel_id", 20}, {"pdu_size", 4}, {"name", "pdu_b"}, {"type", "test_msgs/PduB"}}
        }).dump(2);
    }
    {
        std::ofstream ofs(pdudef_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"paths", nlohmann::json::array({{{"id", "default"}, {"path", pdutypes_path.string()}}})},
            {"robots", nlohmann::json::array({{{"name", "RobotA"}, {"pdutypes_id", "default"}}})}
        }.dump(2);
    }
    {
        std::ofstream ofs(comm_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"protocol", "storage"},
            {"direction", "out"},
            {"comm_raw_version", "v2"},
            {"storage", {
                {"backend", "file"},
                {"mode", "latest"},
                {"path", storage_path.string()}
            }}
        }.dump(2);
    }
    {
        std::ofstream ofs(endpoint_path);
        ASSERT_TRUE(ofs.is_open());
        ofs << nlohmann::json{
            {"name", "storage_latest_debug_endpoint"},
            {"pdu_def_path", pdudef_path.string()},
            {"cache", cache_path},
            {"comm", comm_path.string()}
        }.dump(2);
    }

    hakoniwa::pdu::Endpoint endpoint("storage_latest_debug_json", HAKO_PDU_ENDPOINT_DIRECTION_OUT);
    ASSERT_EQ(endpoint.open(endpoint_path.string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 10), std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.send(create_key("RobotA", 20), std::vector<std::byte>{std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D}}), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.stop(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(endpoint.close(), HAKO_PDU_ERR_OK);

    const std::string command = std::string(HAKO_PDU_STORAGE_DEBUG_EXE)
        + " \"" + storage_path.string() + "\" --json > \"" + output_path.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);

    nlohmann::json output = nlohmann::json::parse(read_text_file(output_path));
    EXPECT_EQ(output.at("mode").get<std::string>(), "latest");
    ASSERT_EQ(output.at("records").size(), 2U);
    EXPECT_EQ(output.at("records").at(0).at("key").at("robot_name").get<std::string>(), "RobotA");
    EXPECT_TRUE(output.at("records").at(0).at("initialized").get<bool>());
    EXPECT_FALSE(output.at("records").at(0).at("storage_timestamp_ns").is_null());
    EXPECT_TRUE(output.at("records").at(0).contains("packet_offset"));
}
#endif
