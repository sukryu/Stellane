#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <typeinfo>
#include <type_traits>

namespace stellane {

// Forward declarations
class Logger;

/**

- @brief Request-scoped context container for Stellane framework
- 
- Context는 각 HTTP 요청마다 생성되는 비동기 안전한 데이터 컨테이너입니다.
- 미들웨어와 핸들러 간의 데이터 공유, 분산 추적, 로깅을 위한 핵심 컴포넌트입니다.
- 
- 주요 특징:
- - 요청별 고유한 trace_id 제공
- - 타입 안전한 key-value 저장소
- - 비동기(coroutine) 흐름에서 안전한 전파
- - 자동 로깅 컨텍스트 연결
- 
- @example
- ```cpp
  
  ```
- // 미들웨어에서 사용자 정보 저장
- ctx.set<int>(“user_id”, 42);
- ctx.set<std::string>(“role”, “admin”);
- 
- // 핸들러에서 정보 조회
- auto user_id = ctx.get<int>(“user_id”);
- if (user_id) {
- ```
  ctx.log("Processing request for user " + std::to_string(*user_id));
  ```
- }
- ```
  
  ```

*/
class Context {
public:
/**
* @brief Context 생성자 (프레임워크 내부용)
* @param trace_id 요청 고유 식별자
* @note 일반적으로 Server가 요청 수신 시 자동 생성
*/
explicit Context(std::string trace_id);

```
/**
 * @brief 복사 생성자 (deleted - Context는 이동만 가능)
 */
Context(const Context&) = delete;
Context& operator=(const Context&) = delete;

/**
 * @brief 이동 생성자
 */
Context(Context&&) = default;
Context& operator=(Context&&) = default;

/**
 * @brief 소멸자
 */
~Context() = default;

// ========================================================================
// Trace ID 관리
// ========================================================================

/**
 * @brief 현재 요청의 trace ID 반환
 * @return 요청 고유 식별자 (예: "0a17f28c-d8b4-11ed-b5ea")
 * 
 * 분산 추적, 로깅, 디버깅에서 요청을 고유하게 식별하는 데 사용됩니다.
 */
[[nodiscard]] const std::string& trace_id() const noexcept;

// ========================================================================
// 타입 안전한 데이터 저장소
// ========================================================================

/**
 * @brief 키-값 쌍을 Context에 저장
 * @tparam T 저장할 값의 타입
 * @param key 식별 키
 * @param value 저장할 값
 * 
 * @example
 * ```cpp
 * ctx.set<int>("user_id", 42);
 * ctx.set("session_token", std::string("abc123"));
 * ctx.set("is_authenticated", true);
 * ```
 */
template<typename T>
void set(const std::string& key, T value);

/**
 * @brief 저장된 값을 타입 안전하게 조회
 * @tparam T 조회할 값의 타입
 * @param key 식별 키
 * @return 값이 존재하고 타입이 일치하면 값, 아니면 std::nullopt
 * 
 * @example
 * ```cpp
 * auto user_id = ctx.get<int>("user_id");
 * if (user_id.has_value()) {
 *     // user_id 사용
 *     process_user(*user_id);
 * }
 * ```
 */
template<typename T>
[[nodiscard]] std::optional<T> get(const std::string& key) const;

/**
 * @brief 키가 존재하는지 확인
 * @param key 확인할 키
 * @return 키 존재 여부
 */
[[nodiscard]] bool has(const std::string& key) const noexcept;

/**
 * @brief 특정 키 제거
 * @param key 제거할 키
 * @return 제거 성공 여부 (키가 존재했으면 true)
 */
bool remove(const std::string& key);

/**
 * @brief 모든 데이터 제거
 */
void clear() noexcept;

/**
 * @brief 저장된 키의 개수 반환
 * @return 키-값 쌍의 개수
 */
[[nodiscard]] size_t size() const noexcept;

/**
 * @brief Context가 비어있는지 확인
 * @return 저장된 데이터가 없으면 true
 */
[[nodiscard]] bool empty() const noexcept;

// ========================================================================
// 로깅 인터페이스
// ========================================================================

/**
 * @brief trace_id를 포함한 로그 메시지 출력
 * @param message 로그 메시지
 * 
 * 출력 형식: [timestamp] [level] [trace_id=xxx] message
 * 
 * @example
 * ```cpp
 * ctx.log("User authentication successful");
 * // Output: [2025-07-07T14:03:12Z] [INFO] [trace_id=abc-123] User authentication successful
 * ```
 */
void log(const std::string& message) const;

/**
 * @brief 지정된 로그 레벨로 메시지 출력
 * @param level 로그 레벨 (debug, info, warn, error)
 * @param message 로그 메시지
 */
void log(const std::string& level, const std::string& message) const;

/**
 * @brief 디버그 레벨 로그
 * @param message 로그 메시지
 */
void debug(const std::string& message) const;

/**
 * @brief 정보 레벨 로그
 * @param message 로그 메시지
 */
void info(const std::string& message) const;

/**
 * @brief 경고 레벨 로그
 * @param message 로그 메시지
 */
void warn(const std::string& message) const;

/**
 * @brief 오류 레벨 로그
 * @param message 로그 메시지
 */
void error(const std::string& message) const;

// ========================================================================
// 고급 기능 (향후 확장용)
// ========================================================================

/**
 * @brief 스코프 기반 로그 prefix 설정 (향후 구현 예정)
 * @param scope_name 스코프 이름
 * @return RAII 스타일 스코프 객체
 * 
 * @example
 * ```cpp
 * {
 *     auto scope = ctx.scope("auth");
 *     ctx.log("Starting authentication");  // [auth] Starting authentication
 * }
 * ```
 */
// auto scope(const std::string& scope_name) -> ScopeGuard; // 향후 구현

/**
 * @brief 외부 트레이서 연결 (OpenTelemetry 등)
 * @param tracer 트레이서 객체
 */
// void set_tracer(std::shared_ptr<ITracer> tracer); // 향후 구현
```

private:
// ========================================================================
// 내부 구현
// ========================================================================

```
/// 요청 고유 식별자
std::string trace_id_;

/// 타입 안전한 key-value 저장소
std::unordered_map<std::string, std::any> store_;

/// 로거 인스턴스 (지연 초기화)
mutable std::shared_ptr<Logger> logger_;

/**
 * @brief 로거 인스턴스 가져오기 (지연 초기화)
 * @return 로거 참조
 */
Logger& get_logger() const;

/**
 * @brief std::any로부터 타입 안전한 값 추출
 * @tparam T 추출할 타입
 * @param value std::any 값
 * @return 추출된 값 또는 std::nullopt
 */
template<typename T>
std::optional<T> extract_value(const std::any& value) const;
```

};

// ============================================================================
// 템플릿 구현
// ============================================================================

template<typename T>
void Context::set(const std::string& key, T value) {
static_assert(!std::is_reference_v<T>, “Cannot store references in Context”);
static_assert(std::is_copy_constructible_v<T> || std::is_move_constructible_v<T>,
“Type must be copy or move constructible”);

```
store_[key] = std::make_any<T>(std::forward<T>(value));
```

}

template<typename T>
std::optional<T> Context::get(const std::string& key) const {
auto it = store_.find(key);
if (it == store_.end()) {
return std::nullopt;
}

```
return extract_value<T>(it->second);
```

}

template<typename T>
std::optional<T> Context::extract_value(const std::any& value) const {
try {
// 정확한 타입 매칭 시도
if (value.type() == typeid(T)) {
return std::any_cast<const T&>(value);
}

```
    // const 타입 처리
    if constexpr (!std::is_const_v<T>) {
        if (value.type() == typeid(const T)) {
            return std::any_cast<const T>(value);
        }
    }
    
    // 참조 타입 처리
    if constexpr (std::is_reference_v<T>) {
        using BaseType = std::remove_reference_t<T>;
        if (value.type() == typeid(BaseType)) {
            return std::any_cast<BaseType>(value);
        }
    }
    
    return std::nullopt;
} catch (const std::bad_any_cast&) {
    return std::nullopt;
}
```

}

// ============================================================================
// 편의 함수
// ============================================================================

/**

- @brief 새로운 Context 생성 (팩토리 함수)
- @param trace_id 선택적 trace ID (비어있으면 자동 생성)
- @return 생성된 Context 객체
  */
  [[nodiscard]] Context create_context(const std::string& trace_id = “”);

/**

- @brief 랜덤 trace ID 생성
- @return UUID 형식의 trace ID
  */
  [[nodiscard]] std::string generate_trace_id();

} // namespace stellane
