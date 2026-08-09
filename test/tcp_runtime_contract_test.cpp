#include <gtest/gtest.h>

#include "hakoniwa/pdu/pdu_factory.hpp"
#include "hakoniwa/pdu/socket_portability.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

namespace {

using namespace hakoniwa::pdu;
using namespace std::chrono_literals;

int find_available_tcp_port()
{
    const SocketHandle fd = create_socket(AF_INET, SOCK_STREAM, 0);
    if (!is_valid_socket(fd)) {
        return -1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind_socket(fd, reinterpret_cast<const SocketAddress*>(&address), sizeof(address)) != 0) {
        (void)close_socket(fd);
        return -1;
    }

    SocketLength address_length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<SocketAddress*>(&address), &address_length) != 0) {
        (void)close_socket(fd);
        return -1;
    }
    const int port = ntohs(address.sin_port);
    (void)close_socket(fd);
    return port;
}

class TcpConfigPair {
public:
    TcpConfigPair(int port,
                  int server_read_timeout_ms,
                  int client_read_timeout_ms,
                  int write_timeout_ms)
        : root_(std::filesystem::temp_directory_path()
                / ("hako_tcp_contract_" + std::to_string(port)))
    {
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        server_ = root_ / "server.json";
        client_ = root_ / "client.json";
        write_config(server_, "contract-server", "server", "local", port,
                     server_read_timeout_ms, write_timeout_ms);
        write_config(client_, "contract-client", "client", "remote", port,
                     client_read_timeout_ms, write_timeout_ms);
    }

    ~TcpConfigPair()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    const std::filesystem::path& server() const noexcept { return server_; }
    const std::filesystem::path& client() const noexcept { return client_; }

private:
    static void write_config(const std::filesystem::path& path,
                             const char* name,
                             const char* role,
                             const char* address_key,
                             int port,
                             int read_timeout_ms,
                             int write_timeout_ms)
    {
        std::ofstream stream(path);
        ASSERT_TRUE(stream.is_open());
        stream << "{\n"
               << "  \"protocol\": \"tcp\",\n"
               << "  \"name\": \"" << name << "\",\n"
               << "  \"direction\": \"inout\",\n"
               << "  \"role\": \"" << role << "\",\n"
               << "  \"" << address_key << "\": {\"address\": \"127.0.0.1\", \"port\": " << port << "},\n"
               << "  \"options\": {\n"
               << "    \"read_timeout_ms\": " << read_timeout_ms << ",\n"
               << "    \"write_timeout_ms\": " << write_timeout_ms << ",\n"
               << "    \"blocking\": true\n"
               << "  }\n"
               << "}\n";
    }

    std::filesystem::path root_;
    std::filesystem::path server_;
    std::filesystem::path client_;
};

bool wait_until(const std::function<bool()>& predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return predicate();
}

TEST(TcpRuntimeContractTest, ZeroTimeoutStopUnblocksBlockingReceive)
{
    const int port = find_available_tcp_port();
    ASSERT_GT(port, 0);
    TcpConfigPair configs(port, 0, 0, 0);

    auto server = create_pdu_comm(configs.server().string());
    auto client = create_pdu_comm(configs.client().string());
    ASSERT_NE(server, nullptr);
    ASSERT_NE(client, nullptr);
    ASSERT_EQ(server->open(configs.server().string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client->open(configs.client().string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(server->start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client->start(), HAKO_PDU_ERR_OK);

    ASSERT_TRUE(wait_until([&] {
        bool server_running = false;
        bool client_running = false;
        return server->is_running(server_running) == HAKO_PDU_ERR_OK
            && client->is_running(client_running) == HAKO_PDU_ERR_OK
            && server_running && client_running;
    }, 3s));

    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ(client->stop(), HAKO_PDU_ERR_OK);
    EXPECT_EQ(server->stop(), HAKO_PDU_ERR_OK);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(elapsed, 2s);

    EXPECT_EQ(client->close(), HAKO_PDU_ERR_OK);
    EXPECT_EQ(server->close(), HAKO_PDU_ERR_OK);
}

TEST(TcpRuntimeContractTest, BlockingReceiveTimeoutDisconnectsWithTimeoutError)
{
    const int port = find_available_tcp_port();
    ASSERT_GT(port, 0);

    // Make the timeout owner deterministic. Only the server has a receive
    // deadline; the client waits indefinitely and may observe peer EOF after
    // the server closes the timed-out connection.
    TcpConfigPair configs(port, 100, 0, 1000);

    auto server = create_pdu_comm(configs.server().string());
    auto client = create_pdu_comm(configs.client().string());
    ASSERT_NE(server, nullptr);
    ASSERT_NE(client, nullptr);
    ASSERT_EQ(server->open(configs.server().string()), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client->open(configs.client().string()), HAKO_PDU_ERR_OK);

    std::atomic<int> server_disconnect_reason{HAKO_PDU_ERR_OK};
    ASSERT_EQ(server->set_on_disconnected_callback(
        [&](const CommDisconnectEvent& event) {
            server_disconnect_reason = event.reason_code;
        }), HAKO_PDU_ERR_OK);

    ASSERT_EQ(server->start(), HAKO_PDU_ERR_OK);
    ASSERT_EQ(client->start(), HAKO_PDU_ERR_OK);
    ASSERT_TRUE(wait_until([&] {
        return server_disconnect_reason.load() != HAKO_PDU_ERR_OK;
    }, 5s));
    EXPECT_EQ(server_disconnect_reason.load(), HAKO_PDU_ERR_TIMEOUT);

    EXPECT_EQ(client->stop(), HAKO_PDU_ERR_OK);
    EXPECT_EQ(server->stop(), HAKO_PDU_ERR_OK);
    EXPECT_EQ(client->close(), HAKO_PDU_ERR_OK);
    EXPECT_EQ(server->close(), HAKO_PDU_ERR_OK);
}

} // namespace
