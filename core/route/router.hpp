#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <expected>
#include <coroutine>
#include <concepts>
#include <regex>
#include <chrono>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <atomic>

// Forward declarations from Server
namespace stellane {
enum class NetworkError {
    BIND_FAILED, LISTEN_FAILED, ACCEPT_FAILED, READ_FAILED, WRITE_FAILED,
    TIMEOUT, CONNECTION_CLOSED, INVALID_REQUEST
};

struct Request {
    std::string method;
    std::string path;
    std::string query_string;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::unordered_map<std::string, std::string> path_params; // For dynamic routes

    bool is_websocket_upgrade() const {
        auto it = headers.find("upgrade");
        return it != headers.end() && it->second == "websocket";
    }
};

struct Response {
    int status_code = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    static Response ok(std::string body = "");
    static Response error(int code, std::string message);
    static Response not_found();
    static Response internal_error();
};

// Handler concepts
template<typename Func>
concept SyncHandler = requires(Func f, const Request& req) {
    { f(req) } -> std::convertible_to<std::expected<Response, NetworkError>>;
};

template<typename Func>
concept AsyncHandler = requires(Func f, const Request& req) {
    { f(req) } -> std::convertible_to<std::future<std::expected<Response, NetworkError>>>;
};

// Handler wrapper for type erasure
class HandlerWrapper {
public:
    virtual ~HandlerWrapper() = default;
    virtual std::future<std::expected<Response, NetworkError>> handle(const Request& req) = 0;
    virtual bool is_async() const = 0;
};

template<typename Func>
class SyncHandlerWrapper : public HandlerWrapper {
private:
    Func handler_;
public:
    explicit SyncHandlerWrapper(Func&& handler) : handler_(std::forward<Func>(handler)) {}
    std::future<std::expected<Response, NetworkError>> handle(const Request& req) override;
    bool is_async() const override { return false; }
};

template<typename Func>
class AsyncHandlerWrapper : public HandlerWrapper {
private:
    Func handler_;
public:
    explicit AsyncHandlerWrapper(Func&& handler) : handler_(std::forward<Func>(handler)) {}
    std::future<std::expected<Response, NetworkError>> handle(const Request& req) override;
    bool is_async() const override { return true; }
};

// Route pattern for dynamic routing
struct RoutePattern {
    std::string original_path;
    std::vector<std::string> segments;
    std::vector<bool> is_dynamic;
    std::vector<std::string> param_names;
    std::regex compiled_regex;
    bool has_dynamic_parts = false;

    explicit RoutePattern(std::string_view path);
};

// Route match result
struct RouteMatch {
    bool matched = false;
    std::unordered_map<std::string, std::string> params;

    static RouteMatch no_match() { return RouteMatch{false, {}}; }
    static RouteMatch match(std::unordered_map<std::string, std::string> params = {}) {
        return RouteMatch{true, std::move(params)};
    }
};

// Route matcher using trie for static routes and regex for dynamic
class RouteMatcher {
private:
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
        std::string handler_key;
        bool is_terminal = false;
    };

    std::unique_ptr<TrieNode> static_root_;
    std::vector<std::pair<RoutePattern, std::string>> dynamic_routes_;
    mutable std::shared_mutex mutex_;
    mutable std::unordered_map<std::string, std::pair<std::string, RouteMatch>> route_cache_;

public:
    RouteMatcher();
    void add_route(const std::string& method, const std::string& path, const std::string& handler_key);
    std::pair<std::string, RouteMatch> match_route(const std::string& method, const std::string& path) const;

private:
    void add_static_route(const std::string& route_key, const std::string& handler_key);
    std::string match_static_route(const std::string& route_key) const;
    std::vector<std::string> split_path(const std::string& path) const;
};

// Main Router class
class Router {
private:
    std::unordered_map<std::string, std::unique_ptr<HandlerWrapper>> handlers_;
    RouteMatcher matcher_;
    mutable std::shared_mutex handlers_mutex_;

    struct Metrics {
        std::atomic<uint64_t> total_requests{0};
        std::atomic A<uint64_t> successful_requests{0};
        std::atomic<uint64_t> failed_requests{0};
        std::atomic<uint64_t> avg_response_time_ns{0};
    } metrics_;

public:
    Router();

    // HTTP method registration
    template<SyncHandler Func>
    void get(const std::string& path, Func&& handler) {
        register_handler("GET", path, std::forward<Func>(handler));
    }

    template<SyncHandler Func>
    void post(const std::string& path, Func&& handler) {
        register_handler("POST", path, std::forward<Func>(handler));
    }

    template<SyncHandler Func>
    void put(const std::string& path, Func&& handler) {
        register_handler("PUT", path, std::forward<Func>(handler));
    }

    template<SyncHandler Func>
    void del(const std::string& path, Func&& handler) {
        register_handler("DELETE", path, std::forward<Func>(handler));
    }

    template<AsyncHandler Func>
    void get_async(const std::string& path, Func&& handler) {
        register_async_handler("GET", path, std::forward<Func>(handler));
    }

    template<AsyncHandler Func>
    void websocket(const std::string& path, Func&& handler) {
        register_async_handler("WEBSOCKET", path, std::forward<Func>(handler));
    }

    // Dispatch request
    std::future<std::expected<Response, NetworkError>> dispatch(const Request& request);

    // Configuration loading
    void load_from_config(const std::string& config_path);

    // Metrics
    struct RouterMetrics {
        uint64_t total_requests;
        uint64_t successful_requests;
        uint64_t failed_requests;
        uint64_t avg_response_time_ns;
        double success_rate;
    };
    RouterMetrics get_metrics() const;

private:
    template<SyncHandler Func>
    void register_handler(const std::string& method, const std::string& path, Func&& handler);

    template<AsyncHandler Func>
    void register_async_handler(const std::string& method, const std::string& path, Func&& handler);

    std::future<std::expected<Response, NetworkError>> handle_websocket_upgrade(const Request& request);
    std::string generate_websocket_accept(const std::string& key) const;
};

std::string error_to_string(NetworkError error);
} // namespace stellane
