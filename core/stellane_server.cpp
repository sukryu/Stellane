#include “stellane_server.hpp”
#include <iostream>
#include <thread>
#include <algorithm>
#include <regex>
#include <sstream>
#include <filesystem>
#include <fstream>

namespace stellane {

```
// Request implementation
std::optional<std::string> Request::get_header(const std::string& name) const {
    auto it = headers.find(name);
    return it != headers.end() ? std::optional<std::string>(it->second) : std::nullopt;
}

std::string Request::get_body_as_string() const {
    return std::string(body.begin(), body.end());
}

// Response implementation
void Response::set_header(const std::string& name, const std::string& value) {
    headers[name] = value;
}

void Response::set_body(const std::string& content) {
    body.assign(content.begin(), content.end());
    set_header("Content-Length", std::to_string(body.size()));
}

void Response::set_body(const std::vector<char>& content) {
    body = content;
    set_header("Content-Length", std::to_string(body.size()));
}

std::string Response::to_http_response() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_message << "\r\n";
    
    for (const auto& [key, value] : headers) {
        oss << key << ": " << value << "\r\n";
    }
    
    oss << "\r\n";
    oss.write(body.data(), body.size());
    return oss.str();
}

// Connection implementation
Connection::Connection(int socket_fd) : socket_fd_(socket_fd) {
    read_buffer_.reserve(8192);
    write_buffer_.reserve(8192);
}

Connection::~Connection() {
    if (socket_fd_ != -1) {
#ifdef _WIN32
        closesocket(socket_fd_);
#else
        close(socket_fd_);
#endif
    }
}

std::future<std::expected<Request, NetworkError>> Connection::read_request() {
    return std::async(std::launch::async, [this]() -> std::expected<Request, NetworkError> {
        read_buffer_.clear();
        read_buffer_.resize(8192);
        
#ifdef _WIN32
        int bytes_read = recv(socket_fd_, read_buffer_.data(), read_buffer_.size(), 0);
        if (bytes_read == SOCKET_ERROR) {
            return std::unexpected(NetworkError::READ_FAILED);
        }
#else
        ssize_t bytes_read = recv(socket_fd_, read_buffer_.data(), read_buffer_.size(), 0);
        if (bytes_read < 0) {
            return std::unexpected(NetworkError::READ_FAILED);
        }
#endif
        
        if (bytes_read == 0) {
            return std::unexpected(NetworkError::CONNECTION_CLOSED);
        }
        
        read_buffer_.resize(bytes_read);
        return parse_http_request(read_buffer_);
    });
}

std::future<std::expected<void, NetworkError>> Connection::write_response(const Response& response) {
    return std::async(std::launch::async, [this, response]() -> std::expected<void, NetworkError> {
        std::string http_response = response.to_http_response();
        
#ifdef _WIN32
        int bytes_sent = send(socket_fd_, http_response.c_str(), http_response.length(), 0);
        if (bytes_sent == SOCKET_ERROR) {
            return std::unexpected(NetworkError::WRITE_FAILED);
        }
#else
        ssize_t bytes_sent = send(socket_fd_, http_response.c_str(), http_response.length(), 0);
        if (bytes_sent < 0) {
            return std::unexpected(NetworkError::WRITE_FAILED);
        }
#endif
        
        return {};
    });
}

std::expected<Request, NetworkError> Connection::parse_http_request(const std::vector<char>& data) {
    std::string request_str(data.begin(), data.end());
    std::istringstream iss(request_str);
    
    Request req;
    std::string request_line;
    std::getline(iss, request_line);
    
    // Parse request line: METHOD PATH VERSION
    std::regex request_regex(R"((\w+)\s+([^\s]+)\s+HTTP/([0-9\.]+))");
    std::smatch match;
    if (!std::regex_match(request_line, match, request_regex)) {
        return std::unexpected(NetworkError::INVALID_REQUEST);
    }
    
    req.method = match[1].str();
    req.path = match[2].str();
    req.version = match[3].str();
    
    // Parse headers
    std::string line;
    while (std::getline(iss, line) && line != "\r" && !line.empty()) {
        if (line.back() == '\r') line.pop_back();
        
        auto colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            req.headers[key] = value;
        }
    }
    
    // Check for WebSocket upgrade
    auto upgrade_header = req.get_header("Upgrade");
    auto connection_header = req.get_header("Connection");
    if (upgrade_header && connection_header &&
        upgrade_header.value() == "websocket" &&
        connection_header.value().find("Upgrade") != std::string::npos) {
        req.is_websocket = true;
    }
    
    // Read body if present
    auto content_length_header = req.get_header("Content-Length");
    if (content_length_header) {
        try {
            size_t content_length = std::stoul(content_length_header.value());
            if (content_length > 0) {
                std::string body_str;
                std::string remaining((std::istreambuf_iterator<char>(iss)), std::istreambuf_iterator<char>());
                req.body.assign(remaining.begin(), remaining.end());
            }
        } catch (const std::exception&) {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        }
    }
    
    return req;
}

// STL Network Backend implementation
STLNetworkBackend::STLNetworkBackend() {
#ifdef _WIN32
    WSAStartup(MAKEWORD(2, 2), &wsa_data_);
#endif
}

STLNetworkBackend::~STLNetworkBackend() {
    shutdown();
#ifdef _WIN32
    WSACleanup();
#endif
}

std::expected<void, NetworkError> STLNetworkBackend::bind_and_listen(int port) {
    auto setup_result = setup_socket(port);
    if (!setup_result) {
        return setup_result;
    }
    
    auto event_result = setup_event_loop();
    if (!event_result) {
        return event_result;
    }
    
    running_ = true;
    return {};
}

std::expected<void, NetworkError> STLNetworkBackend::setup_socket(int port) {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
    
    // Enable address reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
    
    if (listen(server_socket_, SOMAXCONN) == -1) {
        return std::unexpected(NetworkError::LISTEN_FAILED);
    }
    
    return {};
}

std::expected<void, NetworkError> STLNetworkBackend::setup_event_loop() {
#ifdef __linux__
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
    
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_socket_;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_socket_, &event) == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
#elif __APPLE__
    kqueue_fd_ = kqueue();
    if (kqueue_fd_ == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
    
    struct kevent event;
    EV_SET(&event, server_socket_, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    
    if (kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) == -1) {
        return std::unexpected(NetworkError::BIND_FAILED);
    }
#endif
    
    return {};
}

std::future<std::expected<ConnectionHandle, NetworkError>> STLNetworkBackend::accept_connections() {
    return std::async(std::launch::async, [this]() -> std::expected<ConnectionHandle, NetworkError> {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, (sockaddr*)&client_addr, &client_len);
        if (client_socket == -1) {
            return std::unexpected(NetworkError::ACCEPT_FAILED);
        }
        
        return std::make_shared<Connection>(client_socket);
    });
}

std::expected<void, NetworkError> STLNetworkBackend::shutdown() {
    running_ = false;
    
    if (server_socket_ != -1) {
#ifdef _WIN32
        closesocket(server_socket_);
#else
        close(server_socket_);
#endif
        server_socket_ = -1;
    }
    
#ifdef __linux__
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
#elif __APPLE__
    if (kqueue_fd_ != -1) {
        close(kqueue_fd_);
        kqueue_fd_ = -1;
    }
#endif
    
    return {};
}

// Server configuration
ServerConfig ServerConfig::from_env() {
    ServerConfig config;
    
    if (const char* port = std::getenv("PORT")) {
        config.port = std::stoi(port);
    }
    
    if (const char* tls = std::getenv("TLS_ENABLED")) {
        config.tls_enabled = std::string(tls) == "true";
    }
    
    if (const char* backend = std::getenv("NETWORK_BACKEND")) {
        config.backend = backend;
    }
    
    return config;
}

// Server implementation
Server::Server(const ServerConfig& config) : config_(config) {
    stl_backend_ = std::make_unique<STLNetworkBackend>();
    
#ifdef STELLANE_USE_UNIFEX
    if (config_.backend == "unifex") {
        unifex_backend_ = std::make_unique<UnifexNetworkBackend>();
    }
#endif
}

Server::~Server() {
    if (running_) {
        shutdown();
    }
}

void Server::use_router(std::shared_ptr<Router> router) {
    router_ = router;
}

std::expected<void, NetworkError> Server::run() {
    if (!router_) {
        return std::unexpected(NetworkError::BIND_FAILED); // No router configured
    }
    
    auto bind_result = stl_backend_->bind_and_listen(config_.port);
    if (!bind_result) {
        return bind_result;
    }
    
    running_ = true;
    
    std::cout << "Stellane Server listening on port " << config_.port << std::endl;
    
    // Main event loop
    while (running_) {
        auto accept_future = stl_backend_->accept_connections();
        auto connection_result = accept_future.get();
        
        if (!connection_result) {
            continue; // Log error and continue
        }
        
        auto connection = connection_result.value();
        
        // Handle connection asynchronously
        std::thread([this, connection]() {
            handle_connection(connection);
        }).detach();
    }
    
    return {};
}

std::expected<void, NetworkError> Server::handle_connection(ConnectionHandle conn) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    active_connections_.push_back(conn);
    
    auto start_time = std::chrono::steady_clock::now();
    
    // Read request
    auto request_future = conn->read_request();
    auto request_result = request_future.get();
    
    if (!request_result) {
        return request_result.error();
    }
    
    auto request = request_result.value();
    
    // Handle WebSocket upgrade
    if (request.is_websocket) {
        websocket_connections_.push_back(conn);
        // WebSocket handling logic here
        return {};
    }
    
    // Dispatch to router
    Response response;
    if (router_) {
        auto router_future = router_->dispatch(request);
        auto router_response = router_future.get();
        response = router_response; // Assuming router returns Response
    } else {
        response.status_code = 404;
        response.status_message = "Not Found";
        response.set_body("Route not found");
    }
    
    // Send response
    auto write_future = conn->write_response(response);
    auto write_result = write_future.get();
    
    // Update stats
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    update_stats(duration);
    
    return write_result;
}

void Server::update_stats(std::chrono::milliseconds response_time) {
    stats_.total_requests++;
    // Simple moving average for response time
    stats_.avg_response_time = (stats_.avg_response_time + response_time) / 2;
}

std::expected<void, NetworkError> Server::shutdown() {
    running_ = false;
    
    // Close all connections
    std::lock_guard<std::mutex> lock(connections_mutex_);
    active_connections_.clear();
    websocket_connections_.clear();
    
    if (stl_backend_) {
        return stl_backend_->shutdown();
    }
    
    return {};
}

Server::Stats Server::get_stats() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    auto stats = stats_;
    stats.active_connections = active_connections_.size();
    stats.websocket_connections = websocket_connections_.size();
    return stats;
}

// Utility functions
std::string error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::BIND_FAILED: return "Failed to bind socket";
        case NetworkError::LISTEN_FAILED: return "Failed to listen on socket";
        case NetworkError::ACCEPT_FAILED: return "Failed to accept connection";
        case NetworkError::READ_FAILED: return "Failed to read from socket";
        case NetworkError::WRITE_FAILED: return "Failed to write to socket";
        case NetworkError::TIMEOUT: return "Operation timed out";
        case NetworkError::CONNECTION_CLOSED: return "Connection closed";
        case NetworkError::INVALID_REQUEST: return "Invalid HTTP request";
        default: return "Unknown error";
    }
}
```

} // namespace stellane
