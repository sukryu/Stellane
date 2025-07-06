#include <iostream>
#include <vector>
#include <functional>
#include <unordered_map>
#include <string>
#include <memory>
#include <chrono> // For high_resolution_clock
#include <ctime>  // For std::time
#include <mutex>  // For std::mutex in rate_limit

// --- Request 및 Response 구조체 캡슐화 강화 ---
struct Request {
private:
    std::string method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::vector<char> body_;
    bool is_websocket_ = false;

public:
    // Constructor (optional, but good practice)
    Request() = default;
    Request(std::string method, std::string path, std::string version = "HTTP/1.1")
        : method_(std::move(method)), path_(std::move(path)), version_(std::move(version)) {}

    // Getters
    const std::string& get_method() const { return method_; }
    const std::string& get_path() const { return path_; }
    const std::string& get_version() const { return version_; }
    bool is_websocket() const { return is_websocket_; }

    std::string get_header(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    const std::vector<char>& get_body_raw() const { return body_; }
    std::string get_body_string() const { return std::string(body_.begin(), body_.end()); }

    // Setters
    void set_method(std::string method) { method_ = std::move(method); }
    void set_path(std::string path) { path_ = std::move(path); }
    void set_version(std::string version) { version_ = std::move(version); }
    void set_websocket(bool ws) { is_websocket_ = ws; }

    void set_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    void set_body(const std::string& content) {
        body_.assign(content.begin(), content.end());
    }
    void set_body_raw(const std::vector<char>& content) {
        body_ = content;
    }
    void set_body_raw(std::vector<char>&& content) {
        body_ = std::move(content);
    }

    // Clear state for object pooling
    void clear() {
        method_.clear();
        path_.clear();
        version_.clear();
        headers_.clear();
        body_.clear();
        is_websocket_ = false;
    }
};

struct Response {
private:
    int status_code_ = 200;
    std::string status_message_ = "OK";
    std::unordered_map<std::string, std::string> headers_;
    std::vector<char> body_;

public:
    // Getters
    int get_status_code() const { return status_code_; }
    const std::string& get_status_message() const { return status_message_; }

    std::string get_header(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : "";
    }

    const std::vector<char>& get_body_raw() const { return body_; }
    std::string get_body_string() const { return std::string(body_.begin(), body_.end()); }


    // Setters
    void set_status(int code, const std::string& message) {
        status_code_ = code;
        status_message_ = message;
    }

    void set_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    void set_body(const std::string& content) {
        body_.assign(content.begin(), content.end());
        set_header("Content-Length", std::to_string(content.length()));
    }
    void set_body_raw(const std::vector<char>& content) {
        body_ = content;
        set_header("Content-Length", std::to_string(content.size()));
    }
    void set_body_raw(std::vector<char>&& content) {
        size_t size = content.size();
        body_ = std::move(content);
        set_header("Content-Length", std::to_string(size));
    }

    void set_json(const std::string& json_content) {
        set_header("Content-Type", "application/json");
        set_body(json_content);
    }

    // Clear state for object pooling
    void clear() {
        status_code_ = 200;
        status_message_ = "OK";
        headers_.clear();
        body_.clear();
    }
};

// 미들웨어 시그니처 정의
using Middleware = std::function<void(Request&, Response&, std::function<void()>)>;

// 미들웨어 체인 실행 결과를 나타내는 열거형
enum class ChainResult {
    COMPLETED,    // 모든 미들웨어가 성공적으로 실행됨
    INTERRUPTED,  // 미들웨어가 next()를 호출하지 않아 중단됨
    ERROR        // 예외 발생
};

// --- MiddlewareChain 클래스 (반복적 방식으로 개선) ---
class MiddlewareChain {
private:
    std::vector<Middleware> middlewares;

public:
    void push_back(const Middleware& middleware) {
        middlewares.push_back(middleware);
    }

    void push_back(Middleware&& middleware) {
        middlewares.push_back(std::move(middleware));
    }

    void clear() {
        middlewares.clear();
    }

    size_t size() const {
        return middlewares.size();
    }

    // 체인 실행 (반복적 방식으로 스택 오버플로우 방지)
    ChainResult handle(Request& req, Response& res) {
        if (middlewares.empty()) {
            return ChainResult::COMPLETED;
        }
        
        try {
            size_t current_index = 0;
            
            while (current_index < middlewares.size()) {
                bool next_called = false;
                
                // next 함수: 다음 미들웨어로 진행
                auto next = [&next_called]() {
                    next_called = true;
                };
                
                // 현재 미들웨어 실행
                middlewares[current_index](req, res, next);
                
                // next()가 호출되지 않았으면 체인 중단
                if (!next_called) {
                    return ChainResult::INTERRUPTED;
                }
                
                ++current_index; // 다음 미들웨어로 이동
            }
            
            return ChainResult::COMPLETED; // 모든 미들웨어 성공적으로 실행됨
            
        } catch (const std::exception& e) {
            std::cerr << "Exception in middleware chain: " << e.what() << std::endl;
            // 예외 발생 시, 응답 상태를 500으로 설정
            if (res.get_status_code() == 200) { // 아직 응답이 설정되지 않았다면
                res.set_status(500, "Internal Server Error");
                res.set_body("An unexpected error occurred.");
            }
            return ChainResult::ERROR;
        } catch (...) {
            std::cerr << "Unknown exception in middleware chain" << std::endl;
            if (res.get_status_code() == 200) {
                res.set_status(500, "Internal Server Error");
                res.set_body("An unknown error occurred.");
            }
            return ChainResult::ERROR;
        }
    }
};

// --- Router 클래스 ---
class Router {
private:
    struct Route {
        std::string method;
        std::string path;
        std::function<void(Request&, Response&)> handler;
        MiddlewareChain middleware_chain;
    };

    std::vector<Route> routes;
    MiddlewareChain global_middleware;

public:
    void use(const Middleware& middleware) {
        global_middleware.push_back(middleware);
    }

    void add_route(const std::string& method, const std::string& path, 
                   std::function<void(Request&, Response&)> handler,
                   const std::vector<Middleware>& middlewares = {}) {
        Route route;
        route.method = method;
        route.path = path;
        route.handler = std::move(handler); // Use move for handler
        
        for (const auto& middleware : middlewares) {
            route.middleware_chain.push_back(middleware);
        }
        
        routes.push_back(std::move(route));
    }

    void add_middleware(const std::string& method, const std::string& path, 
                       const Middleware& middleware) {
        for (auto& route : routes) {
            if (route.method == method && route.path == path) {
                route.middleware_chain.push_back(middleware);
                return;
            }
        }
        std::cerr << "Warning: Route not found for adding middleware: " << method << " " << path << std::endl;
    }

    bool handle_request(Request& req, Response& res) {
        // 글로벌 미들웨어 먼저 실행
        ChainResult global_result = global_middleware.handle(req, res);
        
        if (global_result == ChainResult::INTERRUPTED || global_result == ChainResult::ERROR) {
            return true; // 글로벌 미들웨어가 응답을 처리했거나 오류 발생
        }
        
        // 일치하는 라우트 찾기
        for (auto& route : routes) {
            if (route.method == req.get_method() && route.path == req.get_path()) {
                // 라우트별 미들웨어 실행
                ChainResult route_result = route.middleware_chain.handle(req, res);
                
                if (route_result == ChainResult::INTERRUPTED || route_result == ChainResult::ERROR) {
                    return true; // 라우트 미들웨어가 응답을 처리했거나 오류 발생
                }
                
                // 핸들러 실행
                try {
                    route.handler(req, res);
                    return true;
                } catch (const std::exception& e) {
                    std::cerr << "Exception in route handler for " << req.get_method() << " " << req.get_path() << ": " << e.what() << std::endl;
                    if (res.get_status_code() == 200) { // 아직 응답이 설정되지 않았다면
                        res.set_status(500, "Internal Server Error");
                        res.set_body("Handler error occurred.");
                    }
                    return true;
                } catch (...) {
                    std::cerr << "Unknown exception in route handler for " << req.get_method() << " " << req.get_path() << std::endl;
                     if (res.get_status_code() == 200) {
                        res.set_status(500, "Internal Server Error");
                        res.set_body("An unknown error occurred in handler.");
                    }
                    return true;
                }
            }
        }
        
        // 라우트를 찾지 못함
        res.set_status(404, "Not Found");
        res.set_body("Route not found");
        return true;
    }
};

// --- 예시 미들웨어들 ---
namespace CommonMiddleware {
    Middleware logging() {
        return [](Request& req, Response& res, std::function<void()> next) {
            auto start_time = std::chrono::high_resolution_clock::now();
            std::time_t current_time = std::time(nullptr);
            std::cout << "[" << current_time << "] Incoming request: " << req.get_method() << " " << req.get_path() << std::endl;
            
            next(); // 다음 미들웨어 또는 핸들러 실행
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            std::cout << "[" << current_time << "] Response for " << req.get_path() << ": " << res.get_status_code() << " (" << duration.count() << " us)" << std::endl;
        };
    }

    Middleware cors() {
        return [](Request& req, Response& res, std::function<void()> next) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, X-Trace-Id, X-User-Position"); // Add custom headers
            res.set_header("Access-Control-Max-Age", "86400"); // Cache preflight for 24 hours
            
            if (req.get_method() == "OPTIONS") {
                res.set_status(200, "OK");
                res.set_body("");
                return; // next() 호출하지 않음: preflight 요청은 여기서 끝
            }
            
            next();
        };
    }

    Middleware auth() {
        return [](Request& req, Response& res, std::function<void()> next) {
            std::string auth_header = req.get_header("Authorization");
            if (auth_header.empty() || auth_header != "Bearer valid-token") {
                res.set_status(401, "Unauthorized");
                res.set_json("{\"error\": \"Invalid or missing token\"}");
                return; // next() 호출하지 않음
            }
            // 인증 성공 시 사용자 ID 등을 Request 객체에 추가할 수도 있음 (예: req.set_context("user_id", "some_id"))
            next();
        };
    }

    // IP 기반 속도 제한 미들웨어 (스레드 안전성 고려 및 Retry-After 헤더 추가)
    Middleware rate_limit_per_ip(int max_requests_per_minute = 60) {
        // 실제 구현에서는 Redis나 분산 캐시 사용 권장. 여기서는 예시를 위해 인메모리 static map 사용.
        static std::unordered_map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> ip_counters;
        static std::mutex counter_mutex; // 스레드 안전성을 위해 추가

        return [max_requests_per_minute](Request& req, Response& res, std::function<void()> next) {
            std::string client_ip = req.get_header("X-Real-IP"); // 프록시 뒤 실제 IP
            if (client_ip.empty()) {
                client_ip = req.get_header("X-Forwarded-For"); // 다른 프록시 헤더
            }
            if (client_ip.empty()) {
                client_ip = "127.0.0.1"; // 기본값 또는 실제 클라이언트 IP 추출 로직 필요
            }
            
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            
            std::lock_guard<std::mutex> lock(counter_mutex); // 맵 접근 시 락
            
            auto& [count, last_reset_time] = ip_counters[client_ip];
            
            // 1분(60초)마다 카운터 리셋
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_reset_time).count() >= 60) {
                count = 0;
                last_reset_time = now;
            }
            
            if (++count > max_requests_per_minute) {
                res.set_status(429, "Too Many Requests");
                res.set_json("{\"error\": \"Rate limit exceeded\", \"retry_after\": 60}");
                res.set_header("Retry-After", "60"); // 클라이언트에게 60초 후 재시도 지시
                return;
            }
            
            next();
        };
    }

    // --- 새로운 미들웨어: 분산 추적 미들웨어 (개념적 구현) ---
    Middleware distributed_tracing() {
        return [](Request& req, Response& res, std::function<void()> next) {
            std::string trace_id = req.get_header("X-Trace-Id");
            if (trace_id.empty()) {
                // 실제로는 UUID 생성 라이브러리 사용
                trace_id = "trace_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(std::rand() % 1000); 
            }
            
            // 요청 및 응답에 Trace ID 설정
            req.set_header("X-Trace-Id", trace_id); // 하위 서비스 호출 시 전달용 (Request 객체는 계속 전달되므로)
            res.set_header("X-Trace-Id", trace_id); // 응답 헤더에 Trace ID 포함

            std::cout << "[Trace] Request " << req.get_path() << " Trace ID: " << trace_id << std::endl;
            
            auto start = std::chrono::high_resolution_clock::now();
            next(); // 다음 미들웨어 실행
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            // 실제 시스템에서는 이 정보를 로깅 시스템(예: Jaeger, Zipkin)으로 전송
            std::cout << "[Trace] Response for " << req.get_path() << " in " << duration.count() << " us. Trace ID: " << trace_id << std::endl;
        };
    }

    // --- 새로운 미들웨어: 메타버스 공간 필터링 (개념적 구현) ---
    // 실제 공간 인덱스(예: 쿼드트리, R-트리)와 위치 파싱 로직 필요
    Middleware spatial_filtering() {
        return [](Request& req, Response& res, std::function<void()> next) {
            std::string user_pos_str = req.get_header("X-User-Position");
            if (user_pos_str.empty()) {
                std::cout << "[Spatial] No user position provided." << std::endl;
                next();
                return;
            }

            // 예시: "x,y,z" 형식 파싱
            // 실제로는 더 견고한 파싱 필요
            // float x, y, z;
            // parse_position(user_pos_str, x, y, z); 

            // 개념적인 공간 쿼리 (실제 구현 필요)
            // auto nearby_users = spatial_index.query_range({x,y,z}, message_range);
            // req.set_context("target_users", nearby_users); // Request에 컨텍스트 저장

            std::cout << "[Spatial] Processing request from position: " << user_pos_str << std::endl;
            next();
        };
    }

    // --- 새로운 미들웨어: 성능 모니터링 (개념적 구현) ---
    Middleware performance_monitoring() {
        return [](Request& req, Response& res, std::function<void()> next) {
            auto start = std::chrono::high_resolution_clock::now();
            
            next();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            
            // 실제 시스템에서는 Prometheus exporter나 StatsD 등으로 메트릭 전송
            // record_request_duration_metric(req.get_path(), duration.count());
            // record_status_code_metric(res.get_status_code());
            
            if (duration.count() > 5000) { // 5ms 초과 시 경고
                std::cerr << "[Performance Warning] Slow request: " << req.get_method() << " " 
                          << req.get_path() << " took " << duration.count() << " us" << std::endl;
            } else {
                std::cout << "[Performance] Request " << req.get_path() << " took " << duration.count() << " us" << std::endl;
            }
        };
    }
}

// --- ObjectPool 개념적 구현 (스레드 세이프) ---
// 실제 서버에서는 Request/Response 객체 라이프사이클 관리가 더 복잡해질 수 있음
class ObjectPool {
private:
    std::vector<std::unique_ptr<Request>> request_pool_;
    std::vector<std::unique_ptr<Response>> response_pool_;
    std::mutex request_mutex_;
    std::mutex response_mutex_;
    size_t initial_size_;

public:
    explicit ObjectPool(size_t initial_size = 100) : initial_size_(initial_size) {
        for (size_t i = 0; i < initial_size_; ++i) {
            request_pool_.push_back(std::make_unique<Request>());
            response_pool_.push_back(std::make_unique<Response>());
        }
    }

    std::unique_ptr<Request> get_request() {
        std::lock_guard<std::mutex> lock(request_mutex_);
        if (!request_pool_.empty()) {
            auto req = std::move(request_pool_.back());
            request_pool_.pop_back();
            req->clear(); // Reuse: clear previous state
            return req;
        }
        return std::make_unique<Request>(); // Pool empty, create new
    }
    
    void return_request(std::unique_ptr<Request> req) {
        if (!req) return;
        std::lock_guard<std::mutex> lock(request_mutex_);
        req->clear(); // Clear state before returning to pool
        request_pool_.push_back(std::move(req));
    }

    std::unique_ptr<Response> get_response() {
        std::lock_guard<std::mutex> lock(response_mutex_);
        if (!response_pool_.empty()) {
            auto res = std::move(response_pool_.back());
            response_pool_.pop_back();
            res->clear(); // Reuse: clear previous state
            return res;
        }
        return std::make_unique<Response>(); // Pool empty, create new
    }
    
    void return_response(std::unique_ptr<Response> res) {
        if (!res) return;
        std::lock_guard<std::mutex> lock(response_mutex_);
        res->clear(); // Clear state before returning to pool
        response_pool_.push_back(std::move(res));
    }
};

