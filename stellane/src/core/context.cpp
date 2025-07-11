#include “stellane/core/context.h”
#include “stellane/utils/logger.h”
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace stellane {

// ============================================================================
// Context 구현
// ============================================================================

Context::Context(std::string trace_id)
: trace_id_(std::move(trace_id)) {
// trace_id가 비어있으면 자동 생성
if (trace_id_.empty()) {
trace_id_ = generate_trace_id();
}

```
// 기본 메타데이터 설정
auto now = std::chrono::system_clock::now();
set<std::chrono::system_clock::time_point>("_created_at", now);
set<std::string>("_trace_id", trace_id_);
```

}

const std::string& Context::trace_id() const noexcept {
return trace_id_;
}

bool Context::has(const std::string& key) const noexcept {
return store_.find(key) != store_.end();
}

bool Context::remove(const std::string& key) {
auto it = store_.find(key);
if (it != store_.end()) {
store_.erase(it);
return true;
}
return false;
}

void Context::clear() noexcept {
// 시스템 키들은 보존
std::vector<std::string> system_keys;
for (const auto& [key, value] : store_) {
if (key.starts_with(”_”)) {
system_keys.push_back(key);
}
}

```
store_.clear();

// 시스템 키들 복원
for (const auto& key : system_keys) {
    if (key == "_created_at") {
        set<std::chrono::system_clock::time_point>("_created_at", 
            std::chrono::system_clock::now());
    } else if (key == "_trace_id") {
        set<std::string>("_trace_id", trace_id_);
    }
}
```

}

size_t Context::size() const noexcept {
// 시스템 키들 제외한 개수 반환
size_t count = 0;
for (const auto& [key, value] : store_) {
if (!key.starts_with(”_”)) {
++count;
}
}
return count;
}

bool Context::empty() const noexcept {
return size() == 0;
}

// ============================================================================
// 로깅 구현
// ============================================================================

Logger& Context::get_logger() const {
if (!logger_) {
// 지연 초기화: 첫 로그 호출 시 로거 생성
logger_ = std::make_shared<Logger>(“stellane”, trace_id_);
}
return *logger_;
}

void Context::log(const std::string& message) const {
get_logger().info(message);
}

void Context::log(const std::string& level, const std::string& message) const {
auto& logger = get_logger();

```
if (level == "debug") {
    logger.debug(message);
} else if (level == "info") {
    logger.info(message);
} else if (level == "warn" || level == "warning") {
    logger.warn(message);
} else if (level == "error") {
    logger.error(message);
} else {
    // 알 수 없는 레벨은 info로 처리
    logger.info("[" + level + "] " + message);
}
```

}

void Context::debug(const std::string& message) const {
get_logger().debug(message);
}

void Context::info(const std::string& message) const {
get_logger().info(message);
}

void Context::warn(const std::string& message) const {
get_logger().warn(message);
}

void Context::error(const std::string& message) const {
get_logger().error(message);
}

// ============================================================================
// 편의 함수 구현
// ============================================================================

Context create_context(const std::string& trace_id) {
if (trace_id.empty()) {
return Context(generate_trace_id());
}
return Context(trace_id);
}

std::string generate_trace_id() {
// UUID v4 형식의 trace ID 생성
// 형식: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
// 여기서 x는 랜덤 hex, y는 8,9,a,b 중 하나

```
static thread_local std::random_device rd;
static thread_local std::mt19937 gen(rd());
static thread_local std::uniform_int_distribution<> hex_dist(0, 15);
static thread_local std::uniform_int_distribution<> y_dist(8, 11);

auto hex_char = [&]() -> char {
    int val = hex_dist(gen);
    return val < 10 ? ('0' + val) : ('a' + val - 10);
};

auto y_char = [&]() -> char {
    int val = y_dist(gen);
    return val < 10 ? ('0' + val) : ('a' + val - 10);
};

std::string uuid;
uuid.reserve(36);

// xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
for (int i = 0; i < 8; ++i) uuid += hex_char();
uuid += '-';
for (int i = 0; i < 4; ++i) uuid += hex_char();
uuid += "-4";  // UUID version 4
for (int i = 0; i < 3; ++i) uuid += hex_char();
uuid += '-';
uuid += y_char();  // variant bits
for (int i = 0; i < 3; ++i) uuid += hex_char();
uuid += '-';
for (int i = 0; i < 12; ++i) uuid += hex_char();

return uuid;
```

}

// ============================================================================
// 고성능 특수화 구현
// ============================================================================

namespace detail {

/**

- @brief 자주 사용되는 타입들에 대한 최적화된 구현
- 
- std::any의 오버헤드를 줄이기 위해 자주 사용되는 타입들은
- 별도의 최적화된 저장 방식을 사용할 수 있습니다.
  */
  struct OptimizedStorage {
  // 자주 사용되는 primitive 타입들
  std::unordered_map<std::string, int> ints;
  std::unordered_map<std::string, std::string> strings;
  std::unordered_map<std::string, bool> bools;
  std::unordered_map<std::string, double> doubles;
  
  // 기타 타입들은 std::any 사용
  std::unordered_map<std::string, std::any> others;
  
  bool has_optimized(const std::string& key) const {
  return ints.count(key) || strings.count(key) ||
  bools.count(key) || doubles.count(key);
  }
  };

} // namespace detail

// ============================================================================
// Context 성능 메트릭 및 디버그 정보
// ============================================================================

namespace context_metrics {

struct ContextStats {
size_t total_contexts_created = 0;
size_t total_sets = 0;
size_t total_gets = 0;
size_t total_cache_hits = 0;
size_t total_cache_misses = 0;
std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();
};

// 전역 메트릭 (선택적으로 활성화)
#ifdef STELLANE_ENABLE_CONTEXT_METRICS
thread_local ContextStats g_stats;

void record_context_created() {
++g_stats.total_contexts_created;
}

void record_set_operation() {
++g_stats.total_sets;
}

void record_get_operation(bool cache_hit) {
++g_stats.total_gets;
if (cache_hit) {
++g_stats.total_cache_hits;
} else {
++g_stats.total_cache_misses;
}
}

ContextStats get_stats() {
return g_stats;
}

void reset_stats() {
g_stats = ContextStats{};
}

#else

// 메트릭 비활성화 시 no-op
inline void record_context_created() {}
inline void record_set_operation() {}
inline void record_get_operation(bool) {}
inline ContextStats get_stats() { return {}; }
inline void reset_stats() {}

#endif

} // namespace context_metrics

// ============================================================================
// Context 확장 기능 (향후 구현)
// ============================================================================

#ifdef STELLANE_FUTURE_FEATURES

/**

- @brief 스코프 기반 로그 prefix 관리 (RAII)
  */
  class ScopeGuard {
  public:
  ScopeGuard(Context& ctx, std::string scope_name)
  : ctx_(ctx), scope_name_(std::move(scope_name)) {
  // 이전 스코프 스택에 추가
  auto current_scopes = ctx_.get<std::vector<std::string>>(”*log_scopes”)
  .value_or(std::vector<std::string>{});
  current_scopes.push_back(scope_name*);
  ctx_.set(”_log_scopes”, current_scopes);
  }
  
  ~ScopeGuard() {
  // 스코프 스택에서 제거
  auto current_scopes = ctx_.get<std::vector<std::string>>(”*log_scopes”);
  if (current_scopes && !current_scopes->empty()) {
  current_scopes->pop_back();
  ctx*.set(”_log_scopes”, *current_scopes);
  }
  }
  
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;
  ScopeGuard(ScopeGuard&&) = default;
  ScopeGuard& operator=(ScopeGuard&&) = default;

private:
Context& ctx_;
std::string scope_name_;
};

// Context 클래스에 추가될 메서드
// ScopeGuard Context::scope(const std::string& scope_name) {
//     return ScopeGuard(*this, scope_name);
// }

#endif

// ============================================================================
// 타입 변환 헬퍼 (향후 JSON 직렬화 지원)
// ============================================================================

#ifdef STELLANE_JSON_SUPPORT

#include <nlohmann/json.hpp>

/**

- @brief Context를 JSON으로 직렬화 (디버그용)
  */
  nlohmann::json Context::to_json() const {
  nlohmann::json j;
  j[“trace_id”] = trace_id_;
  
  // 직렬화 가능한 타입들만 포함
  for (const auto& [key, value] : store_) {
  if (key.starts_with(”_”)) continue;  // 시스템 키 제외
  
  ```
   try {
       // 기본 타입들 변환 시도
       if (value.type() == typeid(int)) {
           j["data"][key] = std::any_cast<int>(value);
       } else if (value.type() == typeid(std::string)) {
           j["data"][key] = std::any_cast<std::string>(value);
       } else if (value.type() == typeid(bool)) {
           j["data"][key] = std::any_cast<bool>(value);
       } else if (value.type() == typeid(double)) {
           j["data"][key] = std::any_cast<double>(value);
       } else {
           j["data"][key] = "[non-serializable]";
       }
   } catch (const std::bad_any_cast&) {
       j["data"][key] = "[cast-error]";
   }
  ```
  
  }
  
  return j;
  }

/**

- @brief JSON에서 Context 복원 (테스트용)
  */
  Context Context::from_json(const nlohmann::json& j) {
  Context ctx(j.value(“trace_id”, generate_trace_id()));
  
  if (j.contains(“data”) && j[“data”].is_object()) {
  for (const auto& [key, value] : j[“data”].items()) {
  if (value.is_string()) {
  ctx.set<std::string>(key, value.get<std::string>());
  } else if (value.is_number_integer()) {
  ctx.set<int>(key, value.get<int>());
  } else if (value.is_boolean()) {
  ctx.set<bool>(key, value.get<bool>());
  } else if (value.is_number_float()) {
  ctx.set<double>(key, value.get<double>());
  }
  }
  }
  
  return ctx;
  }

#endif

} // namespace stellane
