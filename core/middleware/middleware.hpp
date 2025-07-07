#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include <chrono>
#include <iostream>
#include <atomic>
#include <regex>
#include <shared_mutex>
#include <unordered_map>
#include <future>
#include <expected>

// Forward declarations
namespace stellane {
struct Request;
struct Response;
enum class NetworkError;
}

namespace stellane {

// 미들웨어 타입 정의
using Middleware = std::function<void(Request&, Response&, std::function<void()>)>;
using PostMiddleware = std::function<void(Request&, Response&)>; // 후처리 훅

// 미들웨어 실행 결과
enum class MiddlewareResult {
COMPLETED,     // 모든 미들웨어가 성공적으로 실행됨
INTERRUPTED,   // 중간에 체인이 중단됨 (next() 호출 안함)
ERROR          // 예외 발생
};

// 미들웨어 실행 컨텍스트
struct MiddlewareContext {
std::chrono::steady_clock::time_point start_time;
std::string current_middleware_name;
std::size_t current_index = 0;
bool chain_completed = false;
std::optional<std::string> error_message;
std::vector<PostMiddleware> post_hooks; // 후처리 훅들

```
// 최대 체인 깊이 제한 (스택 오버플로우 방지)
static constexpr std::size_t MAX_CHAIN_DEPTH = 100;

MiddlewareContext() : start_time(std::chrono::steady_clock::now()) {}

void add_post_hook(PostMiddleware hook) {
    post_hooks.push_back(std::move(hook));
}

void execute_post_hooks(Request& req, Response& res) {
    for (auto& hook : post_hooks) {
        try {
            hook(req, res);
        } catch (...) {
            // 후처리 훅 실패는 무시 (로깅만)
            std::cerr << "Post-hook execution failed" << std::endl;
        }
    }
}
```

};

// 미들웨어 체인 클래스 (재귀 → 반복문 전환)
class MiddlewareChain {
public:
MiddlewareChain() = default;
~MiddlewareChain() = default;

```
// 복사 생성자와 할당 연산자
MiddlewareChain(const MiddlewareChain&) = default;
MiddlewareChain& operator=(const MiddlewareChain&) = default;

// 이동 생성자와 할당 연산자
MiddlewareChain(MiddlewareChain&&) = default;
MiddlewareChain& operator=(MiddlewareChain&&) = default;

// 미들웨어 추가
void push_back(Middleware middleware) {
    middlewares_.push_back(std::move(middleware));
}

// 미들웨어 추가 (이름과 함께)
void push_back(const std::string& name, Middleware middleware) {
    middleware_names_.push_back(name);
    middlewares_.push_back(std::move(middleware));
}

// 미들웨어 체인 실행 (반복문 기반으로 변경)
MiddlewareResult handle(Request& request, Response& response) {
    if (middlewares_.empty()) {
        return MiddlewareResult::COMPLETED;
    }
    
    if (middlewares_.size() > MiddlewareContext::MAX_CHAIN_DEPTH) {
        std::cerr << "Middleware chain too deep: " << middlewares_.size() << std::endl;
        return MiddlewareResult::ERROR;
    }
    
    MiddlewareContext context;
    return execute_chain_iterative(request, response, context);
}

// 미들웨어 체인 크기
std::size_t size() const {
    return middlewares_.size();
}

// 미들웨어 체인이 비어있는지 확인
bool empty() const {
    return middlewares_.empty();
}

// 미들웨어 체인 초기화
void clear() {
    middlewares_.clear();
    middleware_names_.clear();
}

// 디버깅용 미들웨어 이름 목록 반환
std::vector<std::string> get_middleware_names() const {
    return middleware_names_;
}
```

private:
std::vector<Middleware> middlewares_;
std::vector<std::string> middleware_names_;

```
// 반복문 기반 체인 실행 (스택 오버플로우 방지)
MiddlewareResult execute_chain_iterative(Request& request, Response& response, 
                                       MiddlewareContext& context) {
    std::vector<bool> middleware_completed(middlewares_.size(), false);
    std::size_t current_index = 0;
    
    while (current_index < middlewares_.size()) {
        context.current_index = current_index;
        
        // 미들웨어 이름 설정
        if (current_index < middleware_names_.size()) {
            context.current_middleware_name = middleware_names_[current_index];
        } else {
            context.current_middleware_name = "middleware_" + std::to_string(current_index);
        }
        
        bool next_called = false;
        bool should_continue = true;
        
        try {
            // next 함수 정의
            auto next = [&next_called, &should_continue]() {
                if (next_called) {
                    return; // 중복 호출 방지
                }
                next_called = true;
                should_continue = true;
            };
            
            // 현재 미들웨어 실행
            middlewares_[current_index](request, response, next);
            middleware_completed[current_index] = true;
            
            if (!next_called) {
                // next()가 호출되지 않았으면 체인 중단
                context.execute_post_hooks(request, response);
                return MiddlewareResult::INTERRUPTED;
            }
            
            current_index++;
            
        } catch (const std::exception& e) {
            context.error_message = std::string("Error in ") + context.current_middleware_name + ": " + e.what();
            return MiddlewareResult::ERROR;
        } catch (...) {
            context.error_message = std::string("Unknown error in ") + context.current_middleware_name;
            return MiddlewareResult::ERROR;
        }
    }
    
    context.chain_completed = true;
    context.execute_post_hooks(request, response);
    return MiddlewareResult::COMPLETED;
}
```

};

// 패턴 매칭 지원 미들웨어 체인
class PatternMiddlewareChain {
public:
struct PatternEntry {
std::string pattern;
std::regex regex;
std::unique_ptr<MiddlewareChain> chain;

```
    PatternEntry(const std::string& p) : pattern(p), regex(p), chain(std::make_unique<MiddlewareChain>()) {}
};

void add_pattern(const std::string& pattern, const std::string& name, Middleware middleware) {
    auto it = std::find_if(patterns_.begin(), patterns_.end(),
                          [&pattern](const PatternEntry& entry) {
                              return entry.pattern == pattern;
                          });
    
    if (it == patterns_.end()) {
        patterns_.emplace_back(pattern);
        it = patterns_.end() - 1;
    }
    
    it->chain->push_back(name, std::move(middleware));
}

MiddlewareResult handle(const std::string& path, Request& request, Response& response) {
    for (const auto& entry : patterns_) {
        if (std::regex_match(path, entry.regex)) {
            return entry.chain->handle(request, response);
        }
    }
    return MiddlewareResult::COMPLETED;
}

void clear() {
    patterns_.clear();
}
```

private:
std::vector<PatternEntry> patterns_;
};

// 미들웨어 팩토리 클래스 - 확장된 버전
class MiddlewareFactory {
public:
// 로깅 미들웨어 (후처리 훅 포함)
static Middleware create_logger(const std::string& prefix = “[REQUEST]”) {
return [prefix](Request& req, Response& res, std::function<void()> next) {
auto start = std::chrono::steady_clock::now();

```
        std::cout << prefix << " " << req.method << " " << req.path;
        if (!req.query_string.empty()) {
            std::cout << "?" << req.query_string;
        }
        std::cout << std::endl;
        
        next();
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << prefix << " Response: " << res.status_code 
                  << " (" << duration.count() << "ms)" << std::endl;
    };
}

// CORS 미들웨어
static Middleware create_cors(const std::string& origin = "*") {
    return [origin](Request& req, Response& res, std::function<void()> next) {
        res.headers["Access-Control-Allow-Origin"] = origin;
        res.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
        res.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
        
        if (req.method == "OPTIONS") {
            res.status_code = 200;
            res.body = "";
            return;
        }
        
        next();
    };
}

// 경로 패턴 기반 필터 미들웨어
static Middleware create_path_pattern_filter(const std::string& blocked_pattern, 
                                           int status_code = 403, 
                                           const std::string& message = "Access Denied") {
    return [blocked_pattern, status_code, message](Request& req, Response& res, std::function<void()> next) {
        std::regex pattern(blocked_pattern);
        if (std::regex_match(req.path, pattern)) {
            res.status_code = status_code;
            res.body = "{\"error\": \"" + message + "\"}";
            res.headers["content-type"] = "application/json";
            return;
        }
        next();
    };
}

// JWT 인증 미들웨어 (간단한 Bearer token 검증)
static Middleware create_jwt_auth(const std::string& secret_key) {
    return [secret_key](Request& req, Response& res, std::function<void()> next) {
        auto auth_header = req.headers.find("Authorization");
        if (auth_header == req.headers.end()) {
            res.status_code = 401;
            res.body = "{\"error\": \"Authorization header missing\"}";
            res.headers["content-type"] = "application/json";
            return;
        }
        
        std::string token = auth_header->second;
        if (token.substr(0, 7) != "Bearer ") {
            res.status_code = 401;
            res.body = "{\"error\": \"Invalid token format\"}";
            res.headers["content-type"] = "application/json";
            return;
        }
        
        // 실제 JWT 검증 로직은 여기서 구현
        // 현재는 단순 비교로 대체
        if (token.substr(7) != secret_key) {
            res.status_code = 401;
            res.body = "{\"error\": \"Invalid token\"}";
            res.headers["content-type"] = "application/json";
            return;
        }
        
        next();
    };
}

// Rate Limiting 미들웨어
static Middleware create_rate_limiter(std::size_t max_requests_per_minute = 100) {
    // 간단한 in-memory rate limiter (실제로는 Redis 등 사용)
    static std::unordered_map<std::string, std::vector<std::chrono::steady_clock::time_point>> client_requests;
    static std::mutex rate_limit_mutex;
    
    return [max_requests_per_minute](Request& req, Response& res, std::function<void()> next) {
        std::lock_guard<std::mutex> lock(rate_limit_mutex);
        
        // 클라이언트 IP 추출 (실제로는 req.client_ip 등 사용)
        std::string client_id = req.headers.count("X-Forwarded-For") ? 
                              req.headers.at("X-Forwarded-For") : "unknown";
        
        auto now = std::chrono::steady_clock::now();
        auto& requests = client_requests[client_id];
        
        // 1분 이전 요청들 제거
        requests.erase(
            std::remove_if(requests.begin(), requests.end(),
                          [now](const auto& time_point) {
                              return std::chrono::duration_cast<std::chrono::minutes>(now - time_point).count() >= 1;
                          }),
            requests.end()
        );
        
        if (requests.size() >= max_requests_per_minute) {
            res.status_code = 429;
            res.body = "{\"error\": \"Rate limit exceeded\"}";
            res.headers["content-type"] = "application/json";
            res.headers["Retry-After"] = "60";
            return;
        }
        
        requests.push_back(now);
        next();
    };
}

// 요청 크기 제한 미들웨어
static Middleware create_body_size_limit(std::size_t max_size_bytes) {
    return [max_size_bytes](Request& req, Response& res, std::function<void()> next) {
        if (req.body.size() > max_size_bytes) {
            res.status_code = 413;
            res.body = "{\"error\": \"Request body too large\"}";
            res.headers["content-type"] = "application/json";
            return;
        }
        next();
    };
}

// 응답 시간 측정 미들웨어 (후처리 훅 사용)
static Middleware create_timing_middleware(const std::string& header_name = "X-Response-Time") {
    return [header_name](Request& req, Response& res, std::function<void()> next) {
        auto start = std::chrono::steady_clock::now();
        
        next();
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        res.headers[header_name] = std::to_string(duration.count()) + "ms";
    };
}

// 보안 헤더 미들웨어
static Middleware create_security_headers() {
    return [](Request& req, Response& res, std::function<void()> next) {
        next();
        
        // 보안 헤더 추가
        res.headers["X-Content-Type-Options"] = "nosniff";
        res.headers["X-Frame-Options"] = "DENY";
        res.headers["X-XSS-Protection"] = "1; mode=block";
        res.headers["Strict-Transport-Security"] = "max-age=31536000; includeSubDomains";
    };
}
```

};

// 미들웨어 결과를 문자열로 변환
std::string middleware_result_to_string(MiddlewareResult result) {
switch (result) {
case MiddlewareResult::COMPLETED:
return “COMPLETED”;
case MiddlewareResult::INTERRUPTED:
return “INTERRUPTED”;
case MiddlewareResult::ERROR:
return “ERROR”;
default:
return “UNKNOWN”;
}
}

// Router 클래스 확장 - 개선된 미들웨어 기능
class RouterWithMiddleware {
private:
Router base_router_;
MiddlewareChain global_middleware_chain_;
PatternMiddlewareChain pattern_middleware_chain_;
std::unordered_map<std::string, std::unique_ptr<MiddlewareChain>> route_specific_chains_;
mutable std::shared_mutex middleware_mutex_;

```
struct MiddlewareMetrics {
    std::atomic<uint64_t> total_middleware_executions{0};
    std::atomic<uint64_t> interrupted_chains{0};
    std::atomic<uint64_t> error_chains{0};
    std::atomic<uint64_t> avg_middleware_time_ns{0};
    std::atomic<uint64_t> pattern_matches{0};
} middleware_metrics_;
```

public:
RouterWithMiddleware() = default;

```
// 글로벌 미들웨어 추가
void add_global_middleware(Middleware middleware) {
    std::unique_lock lock(middleware_mutex_);
    global_middleware_chain_.push_back(std::move(middleware));
}

void add_global_middleware(const std::string& name, Middleware middleware) {
    std::unique_lock lock(middleware_mutex_);
    global_middleware_chain_.push_back(name, std::move(middleware));
}

// 패턴 기반 미들웨어 추가
void add_pattern_middleware(const std::string& pattern, const std::string& name, Middleware middleware) {
    std::unique_lock lock(middleware_mutex_);
    pattern_middleware_chain_.add_pattern(pattern, name, std::move(middleware));
}

// 특정 경로에 대한 미들웨어 추가
void add_route_middleware(const std::string& method, const std::string& path, 
                        Middleware middleware) {
    std::unique_lock lock(middleware_mutex_);
    std::string route_key = method + ":" + path;
    
    if (route_specific_chains_.find(route_key) == route_specific_chains_.end()) {
        route_specific_chains_[route_key] = std::make_unique<MiddlewareChain>();
    }
    
    route_specific_chains_[route_key]->push_back(std::move(middleware));
}

void add_route_middleware(const std::string& method, const std::string& path, 
                        const std::string& name, Middleware middleware) {
    std::unique_lock lock(middleware_mutex_);
    std::string route_key = method + ":" + path;
    
    if (route_specific_chains_.find(route_key) == route_specific_chains_.end()) {
        route_specific_chains_[route_key] = std::make_unique<MiddlewareChain>();
    }
    
    route_specific_chains_[route_key]->push_back(name, std::move(middleware));
}

// 기존 Router 메서드들을 위임
template<typename Func>
void get(const std::string& path, Func&& handler) {
    base_router_.get(path, std::forward<Func>(handler));
}

template<typename Func>
void post(const std::string& path, Func&& handler) {
    base_router_.post(path, std::forward<Func>(handler));
}

template<typename Func>
void put(const std::string& path, Func&& handler) {
    base_router_.put(path, std::forward<Func>(handler));
}

template<typename Func>
void del(const std::string& path, Func&& handler) {
    base_router_.del(path, std::forward<Func>(handler));
}

// 미들웨어를 적용한 요청 처리 (move 시맨틱 적용)
std::future<std::expected<Response, NetworkError>> dispatch(Request&& request) {
    return std::async(std::launch::async, [this, req = std::move(request)]() -> std::expected<Response, NetworkError> {
        auto start_time = std::chrono::high_resolution_clock::now();
        middleware_metrics_.total_middleware_executions++;
        
        // Request를 mutable로 처리
        Request mutable_request = std::move(req);
        Response response;
        
        // 글로벌 미들웨어 실행
        MiddlewareResult global_result;
        {
            std::shared_lock lock(middleware_mutex_);
            global_result = global_middleware_chain_.handle(mutable_request, response);
        }
        
        if (global_result == MiddlewareResult::INTERRUPTED) {
            middleware_metrics_.interrupted_chains++;
            return response;
        }
        
        if (global_result == MiddlewareResult::ERROR) {
            middleware_metrics_.error_chains++;
            return Response::internal_error();
        }
        
        // 패턴 미들웨어 실행
        {
            std::shared_lock lock(middleware_mutex_);
            auto pattern_result = pattern_middleware_chain_.handle(mutable_request.path, mutable_request, response);
            
            if (pattern_result == MiddlewareResult::INTERRUPTED) {
                middleware_metrics_.interrupted_chains++;
                middleware_metrics_.pattern_matches++;
                return response;
            }
            
            if (pattern_result == MiddlewareResult::ERROR) {
                middleware_metrics_.error_chains++;
                return Response::internal_error();
            }
        }
        
        // 경로별 미들웨어 실행
        std::string route_key = mutable_request.method + ":" + mutable_request.path;
        {
            std::shared_lock lock(middleware_mutex_);
            auto route_chain_it = route_specific_chains_.find(route_key);
            if (route_chain_it != route_specific_chains_.end()) {
                auto route_result = route_chain_it->second->handle(mutable_request, response);
                
                if (route_result == MiddlewareResult::INTERRUPTED) {
                    middleware_metrics_.interrupted_chains++;
                    return response;
                }
                
                if (route_result == MiddlewareResult::ERROR) {
                    middleware_metrics_.error_chains++;
                    return Response::internal_error();
                }
            }
        }
        
        // 기존 라우터로 요청 전달
        auto future_response = base_router_.dispatch(std::move(mutable_request));
        auto router_result = future_response.get();
        
        if (!router_result.has_value()) {
            return std::unexpected(router_result.error());
        }
        
        // 미들웨어 실행 시간 기록
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
        
        uint64_t current_avg = middleware_metrics_.avg_middleware_time_ns.load();
        uint64_t new_avg = (current_avg + duration.count()) / 2;
        middleware_metrics_.avg_middleware_time_ns.store(new_avg);
        
        return router_result.value();
    });
}

// 미들웨어 관련 메트릭스
struct MiddlewareMetrics {
    uint64_t total_middleware_executions;
    uint64_t interrupted_chains;
    uint64_t error_chains;
    uint64_t avg_middleware_time_ns;
    uint64_t pattern_matches;
    double interruption_rate;
    double error_rate;
};

MiddlewareMetrics get_middleware_metrics() const {
    return MiddlewareMetrics{
        middleware_metrics_.total_middleware_executions.load(),
        middleware_metrics_.interrupted_chains.load(),
        middleware_metrics_.error_chains.load(),
        middleware_metrics_.avg_middleware_time_ns.load(),
        middleware_metrics_.pattern_matches.load(),
        static_cast<double>(middleware_metrics_.interrupted_chains.load()) / 
            std::max(1ULL, middleware_metrics_.total_middleware_executions.load()),
        static_cast<double>(middleware_metrics_.error_chains.load()) / 
            std::max(1ULL, middleware_metrics_.total_middleware_executions.load())
    };
}

// 설정 기반 미들웨어 로드
void load_middleware_config(const std::string& config_path) {
    // Config 파일에서 미들웨어 설정 로드
    // 실제 구현에서는 TOML/JSON 파서 사용
    
    // 예시 구성:
    add_global_middleware("logger", MiddlewareFactory::create_logger());
    add_global_middleware("cors", MiddlewareFactory::create_cors());
    add_global_middleware("security", MiddlewareFactory::create_security_headers());
    add_global_middleware("rate_limit", MiddlewareFactory::create_rate_limiter(100));
    
    // 패턴 기반 미들웨어
    add_pattern_middleware("/api/admin/.*", "admin_auth", 
                         MiddlewareFactory::create_jwt_auth("admin_secret"));
    add_pattern_middleware("/api/.*", "api_auth", 
                         MiddlewareFactory::create_jwt_auth("api_secret"));
}

// 미들웨어 체인 초기화
void clear_all_middleware() {
    std::unique_lock lock(middleware_mutex_);
    global_middleware_chain_.clear();
    pattern_middleware_chain_.clear();
    route_specific_chains_.clear();
}
```

};

} // namespace stellane

