#include "hakoniwa/pdu/udp_endpoint.hpp"

#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int find_available_port()
{
    int socket_fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(socket_fd >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    int result = ::bind(socket_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(result == 0);

    socklen_t addr_len = sizeof(addr);
    result = ::getsockname(socket_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
    assert(result == 0);

    int port = ntohs(addr.sin_port);
    ::close(socket_fd);
    return port;
}

std::string write_config(const std::string& filename, const std::string& content)
{
    std::ofstream config_stream(filename);
    assert(config_stream.good());
    config_stream << content;
    config_stream.close();
    return filename;
}

}  // namespace

int main()
{
    const int port = find_available_port();
    const std::string receiver_config_path = "udp_receiver_config.json";
    const std::string sender_config_path = "udp_sender_config.json";

    std::ostringstream receiver_config;
    receiver_config << "{"
                    << "\"protocol\":\"udp\","
                    << "\"name\":\"receiver\","
                    << "\"direction\":\"in\","
                    << "\"address\":\"127.0.0.1\","
                    << "\"port\":" << port << ","
                    << "\"options\":{\"timeout_ms\":1000}"
                    << "}";
    write_config(receiver_config_path, receiver_config.str());

    std::ostringstream sender_config;
    sender_config << "{"
                  << "\"protocol\":\"udp\","
                  << "\"name\":\"sender\","
                  << "\"direction\":\"out\","
                  << "\"address\":\"127.0.0.1\","
                  << "\"port\":" << port
                  << "}";
    write_config(sender_config_path, sender_config.str());

    hakoniwa::pdu::UdpEndpoint receiver("receiver", HAKO_PDU_ENDPOINT_DIRECTION_IN);
    hakoniwa::pdu::UdpEndpoint sender("sender", HAKO_PDU_ENDPOINT_DIRECTION_OUT);

    assert(receiver.open(receiver_config_path) == HAKO_PDU_ERR_OK);
    assert(sender.open(sender_config_path) == HAKO_PDU_ERR_OK);
    assert(receiver.start() == HAKO_PDU_ERR_OK);
    assert(sender.start() == HAKO_PDU_ERR_OK);

    const char payload[] = "ping";
    assert(sender.send(payload, sizeof(payload)) == HAKO_PDU_ERR_OK);

    char buffer[16] = {};
    size_t received_size = 0;
    assert(receiver.recv(buffer, sizeof(buffer), received_size) == HAKO_PDU_ERR_OK);
    assert(received_size == sizeof(payload));
    assert(std::memcmp(buffer, payload, sizeof(payload)) == 0);

    assert(receiver.stop() == HAKO_PDU_ERR_OK);
    assert(sender.stop() == HAKO_PDU_ERR_OK);
    assert(receiver.close() == HAKO_PDU_ERR_OK);
    assert(sender.close() == HAKO_PDU_ERR_OK);

    ::unlink(receiver_config_path.c_str());
    ::unlink(sender_config_path.c_str());

    return 0;
}
