#include "router.hpp"
#include <toml++/toml.hpp>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace stellane {

// Response factory methods
Response Response::ok(std::string body) {
    return Response{200, {{"content-type", "application/json"}}, std::move(body)};
}

Response Response::error(int code, std::string message) {
    return Response{code, {{"content-type", "application/json"}}, "{\"error\": \"" + message + "\"}"};
}

Response Response::not_found() {
    return error(404, "Not Found");
}

Response Response::internal_error() {
    return error(500, "Internal Server Error");
}

// SyncHandlerWrapper implementation
template<typename Func>
std::future<std::expected<Response, NetworkError>> SyncHandlerWrapper<Func>::handle(const Request& req) {
    try {
        auto result = handler_(req);
        return std::async(std::launch::async, [result = std::move(result)]() {
            return result;
        });
    } catch (const std::exception& e) {
        return std::async(std::launch::async, [e]() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }
}

// AsyncHandlerWrapper implementation
template<typename Func>
std::future<std::expected<Response, NetworkError>> AsyncHandlerWrapper<Func>::handle(const Request& req) {
    try {
        return handler_(req);
    } catch (const std::exception& e) {
        return std::async(std::launch::async, []() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }
}

// RoutePattern implementation
RoutePattern::RoutePattern(std::string_view path) : original_path(path) {
    std::string regex_pattern = "^";
    std::string current_segment;

    for (size_t i = 0; i < path.length(); ++i) {
        char c = path[i];
        if (c == '/') {
            if (!current_segment.empty()) {
                segments.push_back(current_segment);
                if (current_segment.starts_with(':')) {
                    is_dynamic.push_back(true);
                    param_names.push_back(current_segment.substr(1));
                    regex_pattern += "([^/]+)";
                    has_dynamic_parts = true;
                } else {
                    is_dynamic.push_back(false);
                    regex_pattern += std::regex_replace(current_segment, std::regex("[.*+?^${}()|[\]\\]"), "\\$&");
                }
                current_segment.clear();
            }
            regex_pattern += "/";
        } else {
            current_segment += c;
        }
    }

    if (!current_segment.empty()) {
        segments.push_back(current_segment);
        if (current_segment.starts_with(':')) {
            is_dynamic.push_back(true);
            param_names.push_back(current_segment.substr(1));
            regex_pattern += "([^/]+)";
            has_dynamic_parts = true;
        } else {
            is_dynamic.push_back(false);
            regex_pattern += std::regex_replace(current_segment, std::regex("[.*+?^${}()|[\]\\]"), "\\$&");
        }
    }

    regex_pattern += "$";
    compiled_regex = std::regex(regex_pattern);
}

// RouteMatcher implementation
RouteMatcher::RouteMatcher() : static_root_(std::make_unique<TrieNode>()) {}

void RouteMatcher::add_route(const std::string& method, const std::string& path, const std::string& handler_key) {
    std::unique_lock lock(mutex_);
    RoutePattern pattern(path);
    std::string full_key = method + ":" + path;

    if (!pattern.has_dynamic_parts) {
        add_static_route(full_key, handler_key);
    } else {
        dynamic_routes_.emplace_back(std::move(pattern), handler_key);
    }
}

std::pair<std::string, RouteMatch> RouteMatcher::match_route(const std::string& method, const std::string& path) const {
    std::shared_lock lock(mutex_);
    std::string cache_key = method + ":" + path;

    // Check cache
    if (auto it = route_cache_.find(cache_key); it != route_cache_.end()) {
        return it->second;
    }

    // Try static routes
    std::string key = method + ":" + path;
    if (auto handler_key = match_static_route(key); !handler_key.empty()) {
        auto result = std::make_pair(handler_key, RouteMatch::match());
        route_cache_.emplace(cache_key, result);
        return result;
    }

    // Try dynamic routes
    for (const auto& [pattern, handler_key] : dynamic_routes_) {
        std::smatch matches;
        if (std::regex_match(path, matches, pattern.compiled_regex)) {
            std::unordered_map<std::string, std::string> params;
            for (size_t i = 1; i < matches.size(); ++i) {
                if (i - 1 < pattern.param_names.size()) {
                    params[pattern.param_names[i - 1]] = matches[i].str();
                }
            }
            auto result = std::make_pair(handler_key, RouteMatch::match(std::move(params)));
            route_cache_.emplace(cache_key, result);
            return result;
        }
    }

    return {"", RouteMatch::no_match()};
}

void RouteMatcher::add_static_route(const std::string& route_key, const std::string& handler_key) {
    TrieNode* current = static_root_.get();
    auto parts = split_path(route_key);

    for (const std::string& part : parts) {
        if (current->children.find(part) == current->children.end()) {
            current->children[part] = std::make_unique<TrieNode>();
        }
        current = current->children[part].get();
    }

    current->is_terminal = true;
    current->handler_key = handler_key;
}

std::string RouteMatcher::match_static_route(const std::string& route_key) const {
    TrieNode* current = static_root_.get();
    auto parts = split_path(route_key);

    for (const std::string& part : parts) {
        auto it = current->children.find(part);
        if (it == current->children.end()) {
            return "";
        }
        current = it->second.get();
    }

    return current->is_terminal ? current->handler_key : "";
}

std::vector<std::string> RouteMatcher::split_path(const std::string& path) const {
    std::vector<std::string> parts;
    std::string current;

    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        parts.push_back(current);
    }

    return parts;
}

// Router implementation
Router::Router() {}

std::future<std::expected<Response, NetworkError>> Router::dispatch(const Request& request) {
    auto start_time = std::chrono::high_resolution_clock::now();
    metrics_.total_requests++;

    if (request.is_websocket_upgrade()) {
        return handle_websocket_upgrade(request);
    }

    auto [handler_key, match_result] = matcher_.match_route(request.method, request.path);
    if (!match_result.matched) {
        metrics_.failed_requests++;
        return std::async(std::launch::async, []() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }

    std::shared_lock lock(handlers_mutex_);
    auto handler_it = handlers_.find(handler_key);
    if (handler_it == handlers_.end()) {
        metrics_.failed_requests++;
        return std::async(std::launch::async, [handler_key]() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }

    Request modified_request = request;
    modified_request.path_params = match_result.params;

    auto result = handler_it->second->handle(modified_request);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);

    if (result.get().has_value()) {
        metrics_.successful_requests++;
    } else {
        metrics_.failed_requests++;
    }

    uint64_t current_avg = metrics_.avg_response_time_ns.load();
    uint64_t new_avg = (current_avg + duration.count()) / 2;
    metrics_.avg_response_time_ns.store(new_avg);

    return result;
}

void Router::load_from_config(const std::string& config_path) {
    try {
        auto config = toml::parse_file(config_path);
        auto router_table = config["router"];
        for (const auto& route : *router_table["routes"].as_array()) {
            std::string method = route["method"].value_or("GET");
            std::string path = route["path"].value_or("/");
            std::string handler_name = route["handler"].value_or("default_handler");
            register_handler(method, path, [handler_name](const Request&) -> std::expected<Response, NetworkError> {
                return Response::ok("Handler: " + handler_name);
            });
        }
        for (const auto& ws_route : *router_table["websocket"].as_array()) {
            std::string path = ws_route["path"].value_or("/ws");
            std::string handler_name = ws_route["handler"].value_or("default_ws_handler");
            register_async_handler("WEBSOCKET", path, [handler_name](const Request&) -> std::future<std::expected<Response, NetworkError>> {
                Response res;
                res.status_code = 101;
                res.headers["upgrade"] = "websocket";
                res.headers["connection"] = "upgrade";
                res.headers["sec-websocket-accept"] = "generated_accept_key"; // Placeholder
                return std::async(std::launch::async, [res]() mutable {
                    return res;
                });
            });
        }
    } catch (const toml::parse_error& e) {
        throw std::runtime_error("Failed to parse TOML: " + std::string(e.what()));
    }
}

Router::RouterMetrics Router::get_metrics() const {
    return RouterMetrics{
        metrics_.total_requests.load(),
        metrics_.successful_requests.load(),
        metrics_.failed_requests.load(),
        metrics_.avg_response_time_ns.load(),
        static_cast<double>(metrics_.successful_requests.load()) / std::max(1ULL, metrics_.total_requests.load())
    };
}

template<typename Func>
void Router::register_handler(const std::string& method, const std::string& path, Func&& handler) {
    std::string handler_key = method + ":" + path;
    {
        std::unique_lock lock(handlers_mutex_);
        handlers_[handler_key] = std::make_unique<SyncHandlerWrapper<Func>>(std::forward<Func>(handler));
    }
    matcher_.add_route(method, path, handler_key);
}

template<typename Func>
void Router::register_async_handler(const std::string& method, const std::string& path, Func&& handler) {
    std::string handler_key = method + ":" + path;
    {
        std::unique_lock lock(handlers_mutex_);
        handlers_[handler_key] = std::make_unique<AsyncHandlerWrapper<Func>>(std::forward<Func>(handler));
    }
    matcher_.add_route(method, path, handler_key);
}

std::future<std::expected<Response, NetworkError>> Router::handle_websocket_upgrade(const Request& request) {
    auto [handler_key, match_result] = matcher_.match_route("WEBSOCKET", request.path);
    if (!match_result.matched) {
        return std::async(std::launch::async, []() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }

    std::shared_lock lock(handlers_mutex_);
    auto handler_it = handlers_.find(handler_key);
    if (handler_it == handlers_.end()) {
        return std::async(std::launch::async, []() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }

    Request modified_request = request;
    modified_request.path_params = match_result.params;

    auto ws_key = request.headers.find("Sec-WebSocket-Key");
    if (ws_key == request.headers.end()) {
        return std::async(std::launch::async, []() {
            return std::unexpected(NetworkError::INVALID_REQUEST);
        });
    }

    Response res;
    res.status_code = 101;
    res.headers["upgrade"] = "websocket";
    res.headers["connection"] = "upgrade";
    res.headers["sec-websocket-accept"] = generate_websocket_accept(ws_key->second);

    auto result = handler_it->second->handle(modified_request);
    return result;
}

std::string Router::generate_websocket_accept(const std::string& key) const {
    std::string magic = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(magic.c_str()), magic.length(), hash);

    std::string result;
    result.resize(EVP_ENCODE_LENGTH(SHA_DIGEST_LENGTH));
    EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&result[0]), hash, SHA_DIGEST_LENGTH);
    return result.substr(0, result.find('='));
}

std::string error_to_string(NetworkError error) {
    switch (error) {
        case NetworkError::BIND_FAILED: return "Failed to bind socket";
        case NetworkError::LISTEN_FAILED: return "Failed to listen on socket";
        case NetworkError::ACCEPT_FAILED: return "Failed to accept connection";
        case NetworkError::READ_FAILED: return "Failed to read from socket";
        case NetworkError::WRITE_FAILED: return "Failed to write to socket";
        case NetworkError::TIMEOUT: return "Operation timed out";
        case NetworkError::CONNECTION_CLOSED: return "Connection closed";
        case NetworkError::INVALID_REQUEST: return "Invalid request";
        default: return "Unknown error";
    }
}

// Explicit template instantiations
template class SyncHandlerWrapper<std::function<std::expected<Response, NetworkError>(const Request&)>>;
template class AsyncHandlerWrapper<std::function<std::future<std::expected<Response, NetworkError>>(const Request&)>>;
} // namespace stellane
