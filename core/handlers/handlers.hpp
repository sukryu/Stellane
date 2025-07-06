// handlers.hpp
#pragma once
#include "Router.hpp"
#include <nlohmann/json.hpp>

namespace stellane {
namespace handlers {
    std::expected<Response, NetworkError> get_health(const Request&) {
        return Response::ok(R"({"status": "healthy", "timestamp": "2025-07-06T12:45:00Z"})");
    }

    std::expected<Response, NetworkError> get_user_by_id(const Request& req) {
        auto id_it = req.path_params.find("id");
        if (id_it == req.path_params.end()) {
            return Response::error(400, "Missing user ID");
        }
        nlohmann::json response = {{"id", id_it->second}, {"name", "User"}};
        return Response::ok(response.dump());
    }

    std::expected<Response, NetworkError> custom_api(const Request&) {
        nlohmann::json response = {{"custom", "This is a custom API"}};
        return Response::ok(response.dump());
    }

    std::future<std::expected<Response, NetworkError>> ws_chat(const Request&) {
        Response res;
        res.status_code = 101;
        res.headers["upgrade"] = "websocket";
        res.headers["connection"] = "upgrade";
        co_await std::chrono::milliseconds(10); // 비동기 작업 시뮬레이션
        co_return res;
    }
}
}
