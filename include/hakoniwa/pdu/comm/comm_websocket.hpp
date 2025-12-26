#pragma once

#include "hakoniwa/pdu/comm/comm_raw.hpp"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/http.hpp>

namespace hakoniwa {
namespace pdu {
namespace comm {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace http = beast::http;
using tcp = net::ip::tcp;

// Forward declaration for the session class that handles a single WebSocket connection
class WebSocketSession;

class WebSocketComm final : public PduCommRaw, public std::enable_shared_from_this<WebSocketComm>
{
public:
    WebSocketComm();
    virtual ~WebSocketComm();
    
    // Method for session to call back to when data is received
    void on_session_data_received(const std::vector<std::byte>& data);

protected:
    // PduCommRaw's pure virtual methods implementation
    HakoPduErrorType raw_open(const std::string& config_path) override;
    HakoPduErrorType raw_close() noexcept override;
    HakoPduErrorType raw_start() noexcept override;
    HakoPduErrorType raw_stop() noexcept override;
    HakoPduErrorType raw_is_running(bool& running) noexcept override;
    HakoPduErrorType raw_send(const std::vector<std::byte>& data) noexcept override;

private:
    enum class Role {
        Client,
        Server
    };

    // Server methods
    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

    // Client methods
    void do_connect();
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type endpoint);
    void on_handshake(beast::error_code ec);

    // Common state
    Role role_ = Role::Client;
    HakoPduEndpointDirectionType config_direction_ = HAKO_PDU_ENDPOINT_DIRECTION_INOUT;
    
    // Boost.Asio & Beast core components
    net::io_context ioc_;
    std::thread comm_thread_;
    std::atomic<bool> is_running_flag_{false};

    // Server-specific components
    tcp::acceptor acceptor_;

    // Client-specific components
    tcp::resolver resolver_;
    std::string remote_host_;
    std::string remote_port_;
    std::string remote_path_ = "/";
    bool secure_ = false;

    // Session management
    std::shared_ptr<WebSocketSession> session_; // For client and the single server session.
                                                // TODO: Server needs to handle multiple sessions.
};

} // namespace comm
} // namespace pdu
} // namespace hakoniwa
