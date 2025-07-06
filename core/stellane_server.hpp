#pragma once

#include <concepts>
#include <expected>
#include <coroutine>
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <optional>
#include <unordered_map>
#include <functional>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/event.h>
#include <unistd.h>
#endif

// Forward declarations
namespace stellane {
class Router;
struct Request;
struct Response;
class Server;

```
// Error types for std::expected
enum class NetworkError {
    BIND_FAILED,
    LISTEN_FAILED,
    ACCEPT_FAILED,
    READ_FAILED,
    WRITE_FAILED,
    TIMEOUT,
    CONNECTION_CLOSED,
    INVALID_REQUEST
};

// Connection handle for tracking active connections
using ConnectionHandle = std::shared_ptr<class Connection>;

// Network backend concept
template<typename T>
concept NetworkBackend = requires(T t, int port, const Request& req, Response& res) {
    { t.bind_and_listen(port) } -> std::same_as<std::expected<void, NetworkError>>;
    { t.accept_connections() } -> std::same_as<std::future<std::expected<ConnectionHandle, NetworkError>>>;
    { t.handle_request(req, res) } -> std::same_as<std::future<std::expected<void, NetworkError>>>;
    { t.shutdown() } -> std::same_as<std::expected<void, NetworkError>>;
};

// Request structure
struct Request {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::vector<char> body;
    bool is_websocket = false;
    
    std::optional<std::string> get_header(const std::string& name) const;
    std::string get_body_as_string() const;
};

// Response structure
struct Response {
    int status_code = 200;
    std::string status_message = "OK";
    std::unordered_map<std::string, std::string> headers;
    std::vector<char> body;
    bool is_websocket_upgrade = false;
    
    void set_header(const std::string& name, const std::string& value);
    void set_body(const std::string& content);
    void set_body(const std::vector<char>& content);
    std::string to_http_response() const;
};

// Connection abstraction
class Connection {
public:
    Connection(int socket_fd);
    ~Connection();
    
    std::future<std::expected<Request, NetworkError>> read_request();
    std::future<std::expected<void, NetworkError>> write_response(const Response& response);
    std::future<std::expected<void, NetworkError>> upgrade_to_websocket();
    
    bool is_websocket() const { return is_websocket_; }
    int get_socket() const { return socket_fd_; }
    
private:
    int socket_fd_;
    bool is_websocket_ = false;
    std::vector<char> read_buffer_;
    std::vector<char> write_buffer_;
    
    std::expected<Request, NetworkError> parse_http_request(const std::vector<char>& data);
    std::expected<void, NetworkError> handle_websocket_handshake(Request& req, Response& res);
};

// STL-based network backend
class STLNetworkBackend {
public:
    STLNetworkBackend();
    ~STLNetworkBackend();
    
    std::expected<void, NetworkError> bind_and_listen(int port);
    std::future<std::expected<ConnectionHandle, NetworkError>> accept_connections();
    std::future<std::expected<void, NetworkError>> handle_request(const Request& req, Response& res);
    std::expected<void, NetworkError> shutdown();
    
private:
    int server_socket_ = -1;
    int epoll_fd_ = -1;  // Linux
    int kqueue_fd_ = -1; // macOS
    bool running_ = false;
    
    std::expected<void, NetworkError> setup_event_loop();
    std::expected<void, NetworkError> setup_socket(int port);
    
#ifdef _WIN32
    WSADATA wsa_data_;
    std::vector<WSAEVENT> events_;
#endif
};

// Unifex-based network backend (requires unifex library)
#ifdef STELLANE_USE_UNIFEX
#include <unifex/task.hpp>
#include <unifex/io_context.hpp>
#include <unifex/scheduler.hpp>

class UnifexNetworkBackend {
public:
    UnifexNetworkBackend();
    ~UnifexNetworkBackend();
    
    std::expected<void, NetworkError> bind_and_listen(int port);
    std::future<std::expected<ConnectionHandle, NetworkError>> accept_connections();
    std::future<std::expected<void, NetworkError>> handle_request(const Request& req, Response& res);
    std::expected<void, NetworkError> shutdown();
    
private:
    unifex::io_context io_context_;
    int server_socket_ = -1;
    bool running_ = false;
    
    unifex::task<std::expected<ConnectionHandle, NetworkError>> accept_connection_task();
    unifex::task<std::expected<void, NetworkError>> handle_request_task(const Request& req, Response& res);
};
#endif

// Server configuration
struct ServerConfig {
    int port = 8080;
    bool tls_enabled = false;
    std::string backend = "stl"; // "stl", "unifex", "asio"
    int max_connections = 1000;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000);
    std::string tls_cert_path;
    std::string tls_key_path;
    
    static ServerConfig from_env();
    static ServerConfig from_toml(const std::string& file_path);
};

// Main server class
class Server {
public:
    explicit Server(const ServerConfig& config = ServerConfig{});
    ~Server();
    
    // Configure router
    void use_router(std::shared_ptr<Router> router);
    
    // Start server (blocking)
    std::expected<void, NetworkError> run();
    
    // Start server (non-blocking)
    std::future<std::expected<void, NetworkError>> run_async();
    
    // Graceful shutdown
    std::expected<void, NetworkError> shutdown();
    
    // WebSocket broadcast
    std::expected<void, NetworkError> broadcast_to_websockets(const std::vector<char>& data);
    
    // Get server statistics
    struct Stats {
        size_t active_connections = 0;
        size_t total_requests = 0;
        size_t websocket_connections = 0;
        std::chrono::milliseconds avg_response_time{0};
    };
    Stats get_stats() const;
    
private:
    ServerConfig config_;
    std::shared_ptr<Router> router_;
    std::unique_ptr<STLNetworkBackend> stl_backend_;
    
#ifdef STELLANE_USE_UNIFEX
    std::unique_ptr<UnifexNetworkBackend> unifex_backend_;
#endif
    
    std::vector<ConnectionHandle> active_connections_;
    std::vector<ConnectionHandle> websocket_connections_;
    mutable std::mutex connections_mutex_;
    
    bool running_ = false;
    Stats stats_;
    
    std::future<void> connection_handler_loop();
    std::expected<void, NetworkError> handle_connection(ConnectionHandle conn);
    std::expected<void, NetworkError> setup_tls();
    
    void cleanup_closed_connections();
    void update_stats(std::chrono::milliseconds response_time);
};

// Utility functions
std::string error_to_string(NetworkError error);
std::expected<std::vector<char>, NetworkError> read_file(const std::string& path);
std::expected<void, NetworkError> write_file(const std::string& path, const std::vector<char>& data);
```

} // namespace stellane
