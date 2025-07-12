#include “stellane/routing/router.h”
#include “stellane/core/task.h”
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <fstream>

namespace stellane {

// ============================================================================
// Router 구현
// ============================================================================

Router::Router() = default;

// ========================================================================
// HTTP 메서드별 라우트 등록
// ========================================================================

Router& Router::get(const std::string& path, HandlerFunction handler) {
return route(“GET”, path, std::move(handler));
}

Router& Router::post(const std::string& path, HandlerFunction handler) {
return route(“POST”, path, std::move(handler));
}

Router& Router::put(const std::string& path, HandlerFunction handler) {
return route(“PUT”, path, std::move(handler));
}

Router& Router::del(const std::string& path, HandlerFunction handler) {
return route(“DELETE”, path, std::move(handler));
}

Router& Router::patch(const std::string& path, HandlerFunction handler) {
return route(“PATCH”, path, std::move(handler));
}

Router& Router::options(const std::string& path, HandlerFunction handler) {
return route(“OPTIONS”, path, std::move(handler));
}

Router& Router::head(const std::string& path, HandlerFunction handler) {
return route(“HEAD”, path, std::move(handler));
}

Router& Router::all(const std::string& path, HandlerFunction handler) {
// 모든 주요 HTTP 메서드에 등록
const std::vector<std::string> methods = {“GET”, “POST”, “PUT”, “DELETE”, “PATCH”, “OPTIONS”, “HEAD”};
for (const auto& method : methods) {
route(method, path, handler);
}
return *this;
}

Router& Router::route(const std::string& method, const std::string& path, HandlerFunction handler) {
auto normalized_method = normalize_method(method);
auto normalized_path = normalize_path(path);

```
if (is_static_path(normalized_path)) {
    add_static_route(normalized_method, normalized_path, std::move(handler));
} else {
    add_dynamic_route(normalized_method, normalized_path, std::move(handler));
}

return *this;
```

}

// ========================================================================
// 라우터 중첩 및 마운트
// ========================================================================

Router& Router::mount(const std::string& prefix, const Router& sub_router) {
auto normalized_prefix = normalize_path(prefix);

```
// 하위 라우터를 shared_ptr로 복사하여 저장
auto sub_router_ptr = std::make_shared<Router>(sub_router);
mounted_routers_.emplace_back(normalized_prefix, sub_router_ptr);

return *this;
```

}

// ========================================================================
// 라우트 매칭
// ========================================================================

std::optional<RouteMatch> Router::match(const std::string& method, const std::string& path) const {
auto normalized_method = normalize_method(method);
auto normalized_path = normalize_path(path);

```
// 1. 정적 라우트 우선 매칭 (O(1) 수준)
if (auto static_match = match_static_route(normalized_method, normalized_path)) {
    return static_match;
}

// 2. 동적 라우트 매칭 (O(n))
if (auto dynamic_match = match_dynamic_route(normalized_method, normalized_path)) {
    return dynamic_match;
}

// 3. 마운트된 라우터에서 매칭
if (auto mounted_match = match_mounted_router(normalized_method, normalized_path)) {
    return mounted_match;
}

return std::nullopt;
```

}

std::optional<RouteMatch> Router::match(const Request& request) const {
return match(request.method(), request.path());
}

// ========================================================================
// 라우트 정보 조회
// ========================================================================

std::vector<std::string> Router::list_routes() const {
std::vector<std::string> routes;

```
// 정적 라우트 수집
for (const auto& [method, trie_root] : static_routes_) {
    std::function<void(const detail::TrieNode*, const std::string&)> collect_routes;
    collect_routes = [&](const detail::TrieNode* node, const std::string& current_path) {
        if (!node->handlers.empty()) {
            for (const auto& [handler_method, handler] : node->handlers) {
                routes.push_back(handler_method + " " + current_path);
            }
        }
        
        for (const auto& [segment, child] : node->children) {
            std::string new_path = current_path.empty() ? segment : current_path + "/" + segment;
            collect_routes(child.get(), new_path);
        }
    };
    
    collect_routes(trie_root.get(), "");
}

// 동적 라우트 수집
for (const auto& [method, dynamic_list] : dynamic_routes_) {
    for (const auto& dynamic_route : dynamic_list) {
        for (const auto& [handler_method, handler] : dynamic_route.handlers) {
            routes.push_back(handler_method + " " + dynamic_route.pattern);
        }
    }
}

// 마운트된 라우터의 라우트들
for (const auto& [prefix, sub_router] : mounted_routers_) {
    auto sub_routes = sub_router->list_routes();
    for (const auto& sub_route : sub_routes) {
        routes.push_back(sub_route + " (mounted at " + prefix + ")");
    }
}

std::sort(routes.begin(), routes.end());
return routes;
```

}

bool Router::has_route(const std::string& method, const std::string& path) const {
return match(method, path).has_value();
}

std::vector<std::string> Router::allowed_methods(const std::string& path) const {
std::vector<std::string> methods;

```
const std::vector<std::string> all_methods = {"GET", "POST", "PUT", "DELETE", "PATCH", "OPTIONS", "HEAD"};

for (const auto& method : all_methods) {
    if (has_route(method, path)) {
        methods.push_back(method);
    }
}

return methods;
```

}

size_t Router::route_count() const {
size_t count = 0;

```
// 정적 라우트 카운트
for (const auto& [method, trie_root] : static_routes_) {
    std::function<void(const detail::TrieNode*)> count_handlers;
    count_handlers = [&](const detail::TrieNode* node) {
        count += node->handlers.size();
        for (const auto& [segment, child] : node->children) {
            count_handlers(child.get());
        }
    };
    count_handlers(trie_root.get());
}

// 동적 라우트 카운트
for (const auto& [method, dynamic_list] : dynamic_routes_) {
    for (const auto& dynamic_route : dynamic_list) {
        count += dynamic_route.handlers.size();
    }
}

return count;
```

}

// ========================================================================
// 디버그 및 유틸리티
// ========================================================================

std::string Router::to_string(bool include_handlers) const {
std::ostringstream oss;
oss << “=== Stellane Router ===\n”;
oss << “Total routes: “ << route_count() << “\n\n”;

```
auto routes = list_routes();
for (const auto& route : routes) {
    oss << route << "\n";
}

if (include_handlers) {
    oss << "\n=== Handler Details ===\n";
    oss << "Static routes: " << static_routes_.size() << " method groups\n";
    oss << "Dynamic routes: " << dynamic_routes_.size() << " method groups\n";
    oss << "Mounted routers: " << mounted_routers_.size() << "\n";
}

return oss.str();
```

}

std::string Router::statistics() const {
std::ostringstream oss;

```
size_t static_count = 0;
size_t dynamic_count = 0;

for (const auto& [method, trie_root] : static_routes_) {
    std::function<void(const detail::TrieNode*)> count_static;
    count_static = [&](const detail::TrieNode* node) {
        static_count += node->handlers.size();
        for (const auto& [segment, child] : node->children) {
            count_static(child.get());
        }
    };
    count_static(trie_root.get());
}

for (const auto& [method, dynamic_list] : dynamic_routes_) {
    for (const auto& dynamic_route : dynamic_list) {
        dynamic_count += dynamic_route.handlers.size();
    }
}

oss << "Router Statistics:\n";
oss << "  Static routes: " << static_count << " (O(1) lookup)\n";
oss << "  Dynamic routes: " << dynamic_count << " (regex matching)\n";
oss << "  Mounted routers: " << mounted_routers_.size() << "\n";

if (static_count + dynamic_count > 0) {
    oss << "  Performance ratio: " << (static_count * 100.0 / (static_count + dynamic_count)) << "% fast routes";
}

return oss.str();
```

}

// ========================================================================
// 내부 헬퍼 메서드
// ========================================================================

bool Router::is_static_path(const std::string& path) {
// 동적 파라미터 (:param)가 포함되어 있으면 동적 경로
return path.find(’:’) == std::string::npos && path.find(’*’) == std::string::npos;
}

std::pair<std::regex, std::vector<std::string>> Router::compile_dynamic_pattern(const std::string& path) {
std::vector<std::string> param_names;
std::string regex_pattern = “^”;

```
auto segments = split_path(path);

for (size_t i = 0; i < segments.size(); ++i) {
    const auto& segment = segments[i];
    
    if (segment.empty()) continue;
    
    regex_pattern += "/";
    
    if (segment[0] == ':') {
        // 동적 파라미터
        std::string param_name = segment.substr(1);
        param_names.push_back(param_name);
        regex_pattern += "([^/]+)";  // 슬래시가 아닌 모든 문자 매칭
    } else if (segment == "*") {
        // 와일드카드 (나머지 모든 경로)
        param_names.push_back("wildcard");
        regex_pattern += "(.*)";
    } else {
        // 정적 세그먼트
        regex_pattern += segment;
    }
}

regex_pattern += "$";

return {std::regex(regex_pattern), param_names};
```

}

std::vector<std::string> Router::split_path(const std::string& path) {
std::vector<std::string> segments;
std::istringstream iss(path);
std::string segment;

```
while (std::getline(iss, segment, '/')) {
    if (!segment.empty()) {
        segments.push_back(segment);
    }
}

return segments;
```

}

std::string Router::normalize_path(const std::string& path) {
if (path.empty()) return “/”;

```
std::string normalized = path;

// 시작에 슬래시 추가
if (normalized[0] != '/') {
    normalized = "/" + normalized;
}

// 끝의 슬래시 제거 (루트 경로 제외)
if (normalized.length() > 1 && normalized.back() == '/') {
    normalized.pop_back();
}

// 연속된 슬래시 제거
size_t pos = 0;
while ((pos = normalized.find("//", pos)) != std::string::npos) {
    normalized.replace(pos, 2, "/");
}

return normalized;
```

}

std::string Router::normalize_method(const std::string& method) {
std::string normalized = method;
std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::toupper);
return normalized;
}

void Router::add_static_route(const std::string& method, const std::string& path, HandlerFunction handler) {
// 메서드별 Trie 루트 생성
if (static_routes_.find(method) == static_routes_.end()) {
static_routes_[method] = std::make_unique<detail::TrieNode>();
}

```
auto segments = split_path(path);
auto* current_node = static_routes_[method].get();

// Trie를 따라 노드 생성/탐색
for (const auto& segment : segments) {
    if (current_node->children.find(segment) == current_node->children.end()) {
        current_node->children[segment] = std::make_unique<detail::TrieNode>();
    }
    current_node = current_node->children[segment].get();
}

// 최종 노드에 핸들러 등록
current_node->handlers[method] = std::move(handler);
```

}

void Router::add_dynamic_route(const std::string& method, const std::string& path, HandlerFunction handler) {
auto [compiled_regex, param_names] = compile_dynamic_pattern(path);

```
// 기존 동적 라우트 목록에서 같은 패턴 찾기
auto& dynamic_list = dynamic_routes_[method];
for (auto& existing_route : dynamic_list) {
    if (existing_route.pattern == path) {
        existing_route.handlers[method] = std::move(handler);
        return;
    }
}

// 새로운 동적 라우트 추가
detail::DynamicRoute new_route(path, std::move(compiled_regex), std::move(param_names));
new_route.handlers[method] = std::move(handler);
dynamic_list.push_back(std::move(new_route));
```

}

std::optional<RouteMatch> Router::match_static_route(const std::string& method, const std::string& path) const {
auto method_it = static_routes_.find(method);
if (method_it == static_routes_.end()) {
return std::nullopt;
}

```
auto segments = split_path(path);
auto* current_node = method_it->second.get();

// Trie를 따라 매칭
for (const auto& segment : segments) {
    auto child_it = current_node->children.find(segment);
    if (child_it == current_node->children.end()) {
        return std::nullopt;
    }
    current_node = child_it->second.get();
}

// 핸들러가 있는지 확인
auto handler_it = current_node->handlers.find(method);
if (handler_it != current_node->handlers.end()) {
    return RouteMatch(handler_it->second, {}, path);
}

return std::nullopt;
```

}

std::optional<RouteMatch> Router::match_dynamic_route(const std::string& method, const std::string& path) const {
auto method_it = dynamic_routes_.find(method);
if (method_it == dynamic_routes_.end()) {
return std::nullopt;
}

```
// 모든 동적 라우트에 대해 매칭 시도
for (const auto& dynamic_route : method_it->second) {
    std::smatch match_result;
    if (std::regex_match(path, match_result, dynamic_route.compiled_regex)) {
        auto handler_it = dynamic_route.handlers.find(method);
        if (handler_it != dynamic_route.handlers.end()) {
            auto params = extract_parameters(match_result, dynamic_route.param_names);
            return RouteMatch(handler_it->second, std::move(params), dynamic_route.pattern);
        }
    }
}

return std::nullopt;
```

}

std::optional<RouteMatch> Router::match_mounted_router(const std::string& method, const std::string& path) const {
for (const auto& [prefix, sub_router] : mounted_routers_) {
if (path.starts_with(prefix)) {
// prefix 제거 후 하위 라우터에서 매칭
std::string sub_path = path.substr(prefix.length());
if (sub_path.empty()) {
sub_path = “/”;
}

```
        if (auto match = sub_router->match(method, sub_path)) {
            // 매칭된 패턴에 prefix 추가
            match->matched_pattern = prefix + match->matched_pattern;
            return match;
        }
    }
}

return std::nullopt;
```

}

std::unordered_map<std::string, std::string> Router::extract_parameters(
const std::smatch& match_result,
const std::vector<std::string>& param_names) {

```
std::unordered_map<std::string, std::string> params;

// match_result[0]은 전체 매칭, match_result[1]부터가 캡처 그룹
for (size_t i = 1; i < match_result.size() && i - 1 < param_names.size(); ++i) {
    params[param_names[i - 1]] = match_result[i].str();
}

return params;
```

}

// ============================================================================
// 편의 함수들
// ============================================================================

HandlerFunction simple_handler(std::function<Task<Response>(const Request&)> handler) {
return [handler = std::move(handler)](Context& ctx, const Request& req) -> Task<Response> {
co_return co_await handler(req);
};
}

HandlerFunction sync_handler(std::function<Response(Context&, const Request&)> handler) {
return [handler = std::move(handler)](Context& ctx, const Request& req) -> Task<Response> {
co_return handler(ctx, req);
};
}

HandlerFunction static_file_handler(const std::string& root_directory, bool enable_directory_listing) {
return [root_directory, enable_directory_listing](Context& ctx, const Request& req) -> Task<Response> {
auto file_path = req.path();

```
    // 보안: 상위 디렉토리 접근 방지
    if (file_path.find("..") != std::string::npos) {
        co_return Response::bad_request("Invalid path");
    }
    
    std::string full_path = root_directory + file_path;
    
    try {
        if (std::filesystem::is_regular_file(full_path)) {
            // 파일 서빙
            co_return Response().with_file(full_path);
        } else if (std::filesystem::is_directory(full_path) && enable_directory_listing) {
            // 디렉토리 목록
            std::ostringstream html;
            html << "<html><head><title>Directory listing</title></head><body>";
            html << "<h1>Directory listing for " << file_path << "</h1><ul>";
            
            for (const auto& entry : std::filesystem::directory_iterator(full_path)) {
                auto filename = entry.path().filename().string();
                html << "<li><a href=\"" << filename;
                if (entry.is_directory()) html << "/";
                html << "\">" << filename;
                if (entry.is_directory()) html << "/";
                html << "</a></li>";
            }
            
            html << "</ul></body></html>";
            co_return Response::ok(html.str()).with_content_type("text/html");
        } else {
            co_return Response::not_found("File not found");
        }
    } catch (const std::exception& e) {
        ctx.error("File serving error: " + std::string(e.what()));
        co_return Response::internal_error("File access error");
    }
};
```

}

HandlerFunction redirect_handler(const std::string& target_url, bool permanent) {
return [target_url, permanent](Context& ctx, const Request& req) -> Task<Response> {
if (permanent) {
co_return Response::moved_permanently(target_url);
} else {
co_return Response::redirect(target_url);
}
};
}

} // namespace stellane
