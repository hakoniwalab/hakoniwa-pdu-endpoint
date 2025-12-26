#include "hakoniwa/pdu/comm/comm_websocket.hpp"
#include "hakoniwa/pdu/socket_utils.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <vector>

namespace hakoniwa {
namespace pdu {
namespace comm {

// Handles a single WebSocket connection (session) for both client and server
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::weak_ptr<WebSocketComm> comm_parent_;
    std::vector<std::vector<std::byte>> write_queue_;
    std::atomic<bool> is_writing_{false};

public:
    // Constructor for server-side sessions
    explicit WebSocketSession(tcp::socket&& socket, std::shared_ptr<WebSocketComm> parent)
        : ws_(std::move(socket)), comm_parent_(parent) {}
    
    // Constructor for client-side sessions
    explicit WebSocketSession(net::io_context& ioc, std::shared_ptr<WebSocketComm> parent)
        : ws_(ioc), comm_parent_(parent) {}

    // Gracefully close the WebSocket connection
    void close() {
        beast::error_code ec;
        ws_.close(websocket::close_code::normal, ec);
    }

    // Start the server-side session
    void start_server_session() {
        ws_.set_option(websocket::stream_base::decorator(
            [](websocket::response_type& res) {
                res.set(http::field::server, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-comm-server");
            }));
        ws_.async_accept(
            beast::bind_front_handler(&WebSocketSession::on_accept, shared_from_this()));
    }
    
    // Start the client-side session
    void start_client_session(const std::string& host, const std::string& path, tcp::resolver::results_type& endpoints) {
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
        beast::get_lowest_layer(ws_).async_connect(endpoints, 
            beast::bind_front_handler(&WebSocketSession::on_connect, shared_from_this()));
    }

    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec) {
            std::cerr << "Session: Client connect error: " << ec.message() << std::endl;
            if (auto parent = comm_parent_.lock()) {
                parent->remove_session(shared_from_this());
            }
            return;
        }
        if (auto parent = comm_parent_.lock()) {
            beast::get_lowest_layer(ws_).expires_never();
            ws_.async_handshake(parent->remote_host_, parent->remote_path_,
                beast::bind_front_handler(&WebSocketSession::on_handshake, shared_from_this()));
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during connect. Terminating session." << std::endl;
            // No need to call remove_session, as the parent is already gone.
        }
    }

    void on_handshake(beast::error_code ec) {
        if(ec) {
            std::cerr << "Session: Client handshake error: " << ec.message() << std::endl;
            if (auto parent = comm_parent_.lock()) {
                parent->remove_session(shared_from_this());
            }
            return;
        }
        std::cout << "Session: Client handshake successful." << std::endl;
        if (auto parent = comm_parent_.lock()) {
            do_read();
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during handshake completion. Terminating session." << std::endl;
        }
    }

    void on_accept(beast::error_code ec) {
        if (ec) {
            std::cerr << "Session: Server accept error: " << ec.message() << std::endl;
            if (auto parent = comm_parent_.lock()) {
                parent->remove_session(shared_from_this());
            }
            return;
        }
        std::cout << "Session: Server connection accepted." << std::endl;
        if (auto parent = comm_parent_.lock()) {
            do_read();
        } else {
            std::cerr << "Session: Parent comm object is no longer valid after accept. Terminating session." << std::endl;
        }
    }

    void do_read() {
        if (auto parent = comm_parent_.lock()) {
            ws_.async_read(buffer_, beast::bind_front_handler(&WebSocketSession::on_read, shared_from_this()));
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during read initiation. Terminating session." << std::endl;
        }
    }

    void on_read(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (ec == websocket::error::closed) {
            std::cout << "Session: Connection closed." << std::endl;
            if (auto self = shared_from_this()) { // Capture self to ensure it's alive for close()
                self->close(); // Explicitly close the session
                if (auto parent = comm_parent_.lock()) {
                    parent->remove_session(self);
                }
            }
            return;
        }
        if (ec) {
            std::cerr << "Session: Read error: " << ec.message() << std::endl;
            if (auto self = shared_from_this()) { // Capture self to ensure it's alive for close()
                self->close(); // Explicitly close the session
                if (auto parent = comm_parent_.lock()) {
                    parent->remove_session(self);
                }
            }
            return;
        }

        if (auto parent = comm_parent_.lock()) {
            const auto& data = buffer_.data();
            std::vector<std::byte> received_data(
                static_cast<const std::byte*>(data.data()), 
                static_cast<const std::byte*>(data.data()) + data.size());
            parent->on_session_data_received(received_data);
            buffer_.consume(buffer_.size());
            do_read();
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during read completion. Terminating session." << std::endl;
            if (auto self = shared_from_this()) {
                self->close(); // Explicitly close the session
            }
        }
    }

    void do_write(const std::vector<std::byte>& data) {
        if (auto parent = comm_parent_.lock()) {
            net::post(ws_.get_executor(), [self = shared_from_this(), data](){
                if (auto p = self->comm_parent_.lock()) { // Check again inside the lambda
                    bool was_empty = self->write_queue_.empty();
                    self->write_queue_.push_back(data);
                    if (was_empty && !self->is_writing_) {
                        self->process_write_queue();
                    }
                } else {
                    std::cerr << "Session: Parent comm object is no longer valid during write initiation lambda. Terminating write." << std::endl;
                    if (auto self_inner = self) {
                        self_inner->close(); // Explicitly close the session
                    }
                }
            });
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during write initiation. Terminating write." << std::endl;
            if (auto self = shared_from_this()) {
                self->close(); // Explicitly close the session
            }
        }
    }

private:
    void process_write_queue() {
        if (write_queue_.empty()) {
            is_writing_ = false;
            return;
        }
        if (auto parent = comm_parent_.lock()) { // Check parent before async_write
            is_writing_ = true;
            ws_.binary(true);
            ws_.async_write(net::buffer(write_queue_.front()),
                beast::bind_front_handler(&WebSocketSession::on_write, shared_from_this()));
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during write queue processing. Terminating write." << std::endl;
            is_writing_ = false; // Reset writing flag
            write_queue_.clear(); // Clear pending writes
            if (auto self = shared_from_this()) {
                self->close(); // Explicitly close the session
            }
        }
    }

    void on_write(beast::error_code ec, std::size_t bytes_transferred) {
        is_writing_ = false;
        boost::ignore_unused(bytes_transferred);
        if (ec) {
            std::cerr << "Session: Write error: " << ec.message() << std::endl;
            if (auto self = shared_from_this()) { // Capture self to ensure it's alive for close()
                self->close(); // Explicitly close the session
                if (auto parent = comm_parent_.lock()) {
                    parent->remove_session(self);
                }
            }
            return;
        }
        if (auto parent = comm_parent_.lock()) {
            write_queue_.erase(write_queue_.begin());
            if (!write_queue_.empty()) {
                process_write_queue();
            }
        } else {
            std::cerr << "Session: Parent comm object is no longer valid during write completion. Clearing write queue." << std::endl;
            if (auto self = shared_from_this()) {
                self->close(); // Explicitly close the session
            }
            write_queue_.clear(); // Clear pending writes
        }
    }
};

// WebSocketComm implementation
WebSocketComm::WebSocketComm()
    : acceptor_(ioc_), resolver_(ioc_), work_guard_(std::in_place, ioc_.get_executor()) {}

WebSocketComm::~WebSocketComm() {
    raw_close();
}

HakoPduErrorType WebSocketComm::raw_open(const std::string& config_path) {
    if (is_running_flag_) return HAKO_PDU_ERR_BUSY;
    std::ifstream config_stream(config_path);
    if (!config_stream) return HAKO_PDU_ERR_IO_ERROR;
    nlohmann::json config_json;
    try { config_stream >> config_json; } catch (const nlohmann::json::exception& e) { return HAKO_PDU_ERR_INVALID_ARGUMENT; }

    config_direction_ = parse_direction(config_json.at("direction").get<std::string>());
    const std::string role_value = config_json.at("role").get<std::string>();
    if (role_value == "server") {
        role_ = Role::Server;
        unsigned short port = config_json.at("local").value("port", 8080);
        tcp::endpoint endpoint(net::ip::make_address("0.0.0.0"), port);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(net::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
    } else {
        role_ = Role::Client;
        const auto& remote_cfg = config_json.at("remote");
        remote_host_ = remote_cfg.value("host", "127.0.0.1");
        remote_port_ = std::to_string(remote_cfg.value("port", 8080));
        remote_path_ = remote_cfg.value("path", "/");
    }
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType WebSocketComm::raw_close() noexcept {
    raw_stop();
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType WebSocketComm::raw_start() noexcept {
    if (is_running_flag_) return HAKO_PDU_ERR_BUSY;
    is_running_flag_ = true;
    
    comm_thread_ = std::thread([this]() { ioc_.run(); });

    if (role_ == Role::Server) do_accept();
    else do_connect();

    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType WebSocketComm::raw_stop() noexcept {
    if (!is_running_flag_) return HAKO_PDU_ERR_OK;
    is_running_flag_ = false;

    // Close acceptor to stop accepting new connections
    if (acceptor_.is_open()) {
        acceptor_.cancel(); // Cancel any pending accept operations
        acceptor_.close();
    }
    
    // Clear sessions and ensure their resources are released.
    // This will cause sessions to destruct, which might implicitly cancel their handlers.
    std::lock_guard<std::mutex> lock(sessions_mtx_);
    for (auto& session : sessions_) {
        if (session) {
            session->close();
        }
    }
    sessions_.clear();
    
    // Reset the work_guard to allow the io_context to stop
    work_guard_.reset();
    
    // Then stop the io_context
    if (!ioc_.stopped()) {
        ioc_.stop();
    }
    
    if (comm_thread_.joinable()) comm_thread_.join(); // Wait for the communication thread to finish

    ioc_.restart(); // Prepare for possible reuse
    
    return HAKO_PDU_ERR_OK;
}

void WebSocketComm::remove_session(std::shared_ptr<WebSocketSession> session_to_remove) {
    std::lock_guard<std::mutex> lock(sessions_mtx_);
    if (session_to_remove) {
        session_to_remove->close(); // Close the session being removed
    }
    sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), session_to_remove), sessions_.end());
    std::cout << "Session removed. Current active sessions: " << sessions_.size() << std::endl;
}

HakoPduErrorType WebSocketComm::raw_is_running(bool& running) noexcept {
    running = is_running_flag_;
    return HAKO_PDU_ERR_OK;
}

HakoPduErrorType WebSocketComm::raw_send(const std::vector<std::byte>& data) noexcept {
    if (!is_running_flag_) return HAKO_PDU_ERR_NOT_RUNNING;
    std::lock_guard<std::mutex> lock(sessions_mtx_);
    if (sessions_.empty()) return HAKO_PDU_ERR_NOT_RUNNING;

    for (const auto& session : sessions_) {
        if (session) {
            session->do_write(data);
        }
    }
    return HAKO_PDU_ERR_OK;
}

void WebSocketComm::on_session_data_received(const std::vector<std::byte>& data) {
    on_raw_data_received(data);
}

void WebSocketComm::do_accept() {
    if (!acceptor_.is_open()) return;
    acceptor_.async_accept(net::make_strand(ioc_),
        beast::bind_front_handler(&WebSocketComm::on_accept, std::static_pointer_cast<WebSocketComm>(shared_from_this())));
}

void WebSocketComm::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
        std::cerr << "Comm: Accept error: " << ec.message() << std::endl;
        // No return here, continue accepting new connections even if one fails
    }
    std::cout << "Comm: Server connection accepted." << std::endl;
    std::lock_guard<std::mutex> lock(this->sessions_mtx_);
    auto session = std::make_shared<WebSocketSession>(std::move(socket), std::static_pointer_cast<WebSocketComm>(this->shared_from_this()));
    this->sessions_.push_back(session);
    session->start_server_session();
    this->do_accept(); // Continue accepting
}

void WebSocketComm::do_connect() {
    this->resolver_.async_resolve(this->remote_host_, this->remote_port_,
        beast::bind_front_handler(&WebSocketComm::on_resolve, std::static_pointer_cast<WebSocketComm>(shared_from_this())));
}

void WebSocketComm::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if (ec) {
        std::cerr << "Comm: Resolve error: " << ec.message() << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lock(this->sessions_mtx_);
    this->sessions_.clear(); // Ensure only one client session
    auto session = std::make_shared<WebSocketSession>(this->ioc_, std::static_pointer_cast<WebSocketComm>(this->shared_from_this()));
    this->sessions_.push_back(session);
    session->start_client_session(this->remote_host_, this->remote_path_, results);
}

} // namespace comm
} // namespace pdu
} // namespace hakoniwa