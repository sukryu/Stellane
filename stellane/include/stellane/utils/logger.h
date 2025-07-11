#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <fstream>

namespace stellane {

// ============================================================================
// 로그 레벨 정의
// ============================================================================

enum class LogLevel : int {
TRACE = 0,
DEBUG = 1,
INFO  = 2,
WARN  = 3,
ERROR = 4,
FATAL = 5,
OFF   = 6
};

/**

- @brief 로그 레벨을 문자열로 변환
  */
  [[nodiscard]] std::string to_string(LogLevel level);

/**

- @brief 문자열을 로그 레벨로 변환
  */
  [[nodiscard]] LogLevel from_string(const std::string& level_str);

// ============================================================================
// 로그 메시지 구조체
// ============================================================================

struct LogMessage {
LogLevel level;
std::string component;
std::string trace_id;
std::string message;
std::chrono::system_clock::time_point timestamp;
std::thread::id thread_id;

```
// 구조화된 필드 (key-value 쌍)
std::unordered_map<std::string, std::string> fields;

LogMessage() = default;
LogMessage(LogLevel lvl, std::string comp, std::string trace, 
           std::string msg, std::chrono::system_clock::time_point ts = 
           std::chrono::system_clock::now());
```

};

// ============================================================================
// 로그 포매터 인터페이스
// ============================================================================

class ILogFormatter {
public:
virtual ~ILogFormatter() = default;

```
/**
 * @brief 로그 메시지를 포맷팅
 * @param message 로그 메시지
 * @return 포맷팅된 문자열
 */
virtual std::string format(const LogMessage& message) = 0;
```

};

/**

- @brief 기본 텍스트 포매터
- 형식: [2025-07-12T10:30:45.123Z] [INFO] [component] [trace_id] message
  */
  class TextFormatter : public ILogFormatter {
  public:
  explicit TextFormatter(bool include_thread_id = false);
  std::string format(const LogMessage& message) override;

private:
bool include_thread_id_;
};

/**

- @brief JSON 포매터 (구조화된 로깅)
- {“timestamp”:“2025-07-12T10:30:45.123Z”,“level”:“INFO”,“component”:“stellane”,“trace_id”:“abc-123”,“message”:“test”}
  */
  class JsonFormatter : public ILogFormatter {
  public:
  explicit JsonFormatter(bool pretty_print = false);
  std::string format(const LogMessage& message) override;

private:
bool pretty_print_;
};

// ============================================================================
// 로그 싱크 인터페이스
// ============================================================================

class ILogSink {
public:
virtual ~ILogSink() = default;

```
/**
 * @brief 로그 메시지 출력
 * @param formatted_message 포맷팅된 로그 메시지
 */
virtual void write(const std::string& formatted_message) = 0;

/**
 * @brief 버퍼 플러시 (즉시 출력)
 */
virtual void flush() = 0;
```

};

/**

- @brief 콘솔 출력 싱크
  */
  class ConsoleSink : public ILogSink {
  public:
  explicit ConsoleSink(bool use_colors = true);
  void write(const std::string& formatted_message) override;
  void flush() override;

private:
bool use_colors_;
std::mutex mutex_;  // 멀티스레드 안전성

```
std::string get_color_code(LogLevel level) const;
```

};

/**

- @brief 파일 출력 싱크
  */
  class FileSink : public ILogSink {
  public:
  explicit FileSink(const std::string& filename, bool append = true);
  ~FileSink();
  
  void write(const std::string& formatted_message) override;
  void flush() override;
  
  /**
  - @brief 로그 파일 로테이션
  - @param max_size_mb 최대 파일 크기 (MB)
  - @param max_files 보관할 파일 개수
    */
    void enable_rotation(size_t max_size_mb = 100, size_t max_files = 10);

private:
std::string filename_;
std::ofstream file_;
std::mutex mutex_;

```
// 로테이션 설정
bool rotation_enabled_ = false;
size_t max_size_bytes_ = 0;
size_t max_files_ = 0;
size_t current_size_ = 0;

void rotate_if_needed();
void perform_rotation();
```

};

/**

- @brief 비동기 로그 싱크 (고성능)
  */
  class AsyncSink : public ILogSink {
  public:
  explicit AsyncSink(std::shared_ptr<ILogSink> underlying_sink,
  size_t buffer_size = 1000);
  ~AsyncSink();
  
  void write(const std::string& formatted_message) override;
  void flush() override;
  
  /**
  - @brief 큐에 대기 중인 로그 개수
    */
    [[nodiscard]] size_t pending_count() const;

private:
std::shared_ptr<ILogSink> underlying_sink_;

```
// 비동기 처리
std::queue<std::string> message_queue_;
mutable std::mutex queue_mutex_;
std::condition_variable queue_cv_;
std::thread worker_thread_;
std::atomic<bool> stop_flag_{false};
size_t buffer_size_;

void worker_loop();
```

};

// ============================================================================
// 메인 Logger 클래스
// ============================================================================

/**

- @brief Stellane의 구조화된 로거
- 
- 특징:
- - 다중 싱크 지원 (콘솔 + 파일 동시 출력 가능)
- - 구조화된 필드 지원 (key-value 쌍)
- - Trace ID 자동 포함
- - 비동기 로깅 지원 (고성능)
- - Thread-safe
- 
- @example
- ```cpp
  
  ```
- Logger logger(“auth_service”, “trace-123”);
- logger.info(“User login successful”);
- logger.with_field(“user_id”, “42”)
- ```
    .with_field("ip", "192.168.1.1")
  ```
- ```
    .warn("Suspicious login attempt");
  ```
- ```
  
  ```

*/
class Logger {
public:
/**
* @brief Logger 생성자
* @param component 컴포넌트 이름 (예: “stellane”, “auth_service”)
* @param trace_id 추적 ID (Context에서 전달)
*/
explicit Logger(std::string component, std::string trace_id = “”);

```
/**
 * @brief 복사 생성자
 */
Logger(const Logger& other);
Logger& operator=(const Logger& other);

/**
 * @brief 이동 생성자
 */
Logger(Logger&& other) noexcept;
Logger& operator=(Logger&& other) noexcept;

/**
 * @brief 소멸자
 */
~Logger();

// ========================================================================
// 로그 레벨별 메서드
// ========================================================================

void trace(const std::string& message);
void debug(const std::string& message);
void info(const std::string& message);
void warn(const std::string& message);
void error(const std::string& message);
void fatal(const std::string& message);

/**
 * @brief 지정된 레벨로 로그 출력
 * @param level 로그 레벨
 * @param message 메시지
 */
void log(LogLevel level, const std::string& message);

// ========================================================================
// 구조화된 로깅 (Fluent Interface)
// ========================================================================

/**
 * @brief 구조화된 필드 추가
 * @param key 필드 키
 * @param value 필드 값
 * @return Logger& (체이닝 가능)
 * 
 * @example
 * ```cpp
 * logger.with_field("user_id", "123")
 *       .with_field("action", "login")
 *       .info("User action performed");
 * ```
 */
Logger& with_field(const std::string& key, const std::string& value);

/**
 * @brief 숫자 필드 추가
 */
template<typename T>
Logger& with_field(const std::string& key, T value);

/**
 * @brief 모든 구조화된 필드 제거
 */
Logger& clear_fields();

// ========================================================================
// 설정 메서드
// ========================================================================

/**
 * @brief 최소 로그 레벨 설정
 * @param level 이 레벨 이상만 출력
 */
void set_level(LogLevel level);

/**
 * @brief 현재 최소 로그 레벨 반환
 */
[[nodiscard]] LogLevel get_level() const;

/**
 * @brief 특정 레벨이 활성화되어 있는지 확인
 * @param level 확인할 레벨
 * @return 활성화 여부
 */
[[nodiscard]] bool is_enabled(LogLevel level) const;

/**
 * @brief 로그 싱크 추가
 * @param sink 출력 대상
 */
void add_sink(std::shared_ptr<ILogSink> sink);

/**
 * @brief 모든 싱크 제거
 */
void clear_sinks();

/**
 * @brief 포매터 설정
 * @param formatter 로그 포맷터
 */
void set_formatter(std::shared_ptr<ILogFormatter> formatter);

/**
 * @brief 즉시 플러시 (모든 싱크)
 */
void flush();

// ========================================================================
// 메타데이터 접근
// ========================================================================

/**
 * @brief 컴포넌트 이름 반환
 */
[[nodiscard]] const std::string& component() const;

/**
 * @brief Trace ID 반환
 */
[[nodiscard]] const std::string& trace_id() const;

/**
 * @brief Trace ID 변경
 * @param new_trace_id 새로운 추적 ID
 */
void set_trace_id(const std::string& new_trace_id);
```

private:
std::string component_;
std::string trace_id_;
std::atomic<LogLevel> min_level_{LogLevel::INFO};

```
// 구조화된 필드 (thread-local로 관리될 수 있음)
std::unordered_map<std::string, std::string> fields_;
mutable std::mutex fields_mutex_;

// 출력 관련
std::vector<std::shared_ptr<ILogSink>> sinks_;
std::shared_ptr<ILogFormatter> formatter_;
mutable std::mutex sinks_mutex_;

/**
 * @brief 실제 로그 출력 수행
 * @param level 로그 레벨
 * @param message 메시지
 */
void do_log(LogLevel level, const std::string& message);
```

};

// ============================================================================
// 전역 로거 관리
// ============================================================================

/**

- @brief 로거 팩토리 및 전역 관리자
  */
  class LoggerFactory {
  public:
  /**
  - @brief 기본 로거 반환
  - @return 전역 기본 로거
    */
    static Logger& get_default();
  
  /**
  - @brief 컴포넌트별 로거 생성/반환
  - @param component 컴포넌트 이름
  - @return 컴포넌트 전용 로거
    */
    static Logger& get_logger(const std::string& component);
  
  /**
  - @brief 전역 설정 적용
  - @param level 모든 로거의 최소 레벨
    */
    static void set_global_level(LogLevel level);
  
  /**
  - @brief 기본 싱크 설정 (새로 생성되는 로거에 적용)
  - @param sink 기본 출력 대상
    */
    static void set_default_sink(std::shared_ptr<ILogSink> sink);
  
  /**
  - @brief 기본 포매터 설정
  - @param formatter 기본 포맷터
    */
    static void set_default_formatter(std::shared_ptr<ILogFormatter> formatter);
  
  /**
  - @brief 모든 로거 플러시
    */
    static void flush_all();

private:
static std::unordered_map<std::string, std::unique_ptr<Logger>> loggers_;
static std::mutex loggers_mutex_;
static std::shared_ptr<ILogSink> default_sink_;
static std::shared_ptr<ILogFormatter> default_formatter_;
static LogLevel global_level_;
};

// ============================================================================
// 편의 매크로 (선택적 사용)
// ============================================================================

#ifdef STELLANE_ENABLE_LOG_MACROS

#define STELLANE_TRACE(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::TRACE)) (logger).trace(msg); } while(0)

#define STELLANE_DEBUG(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::DEBUG)) (logger).debug(msg); } while(0)

#define STELLANE_INFO(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::INFO)) (logger).info(msg); } while(0)

#define STELLANE_WARN(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::WARN)) (logger).warn(msg); } while(0)

#define STELLANE_ERROR(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::ERROR)) (logger).error(msg); } while(0)

#define STELLANE_FATAL(logger, msg)   
do { if ((logger).is_enabled(::stellane::LogLevel::FATAL)) (logger).fatal(msg); } while(0)

// 구조화된 로깅 매크로
#define STELLANE_LOG_WITH_FIELDS(logger, level, msg, …)   
do {   
if ((logger).is_enabled(level)) {   
auto temp_logger = logger;   
**VA_ARGS**;   
temp_logger.log(level, msg);   
}   
} while(0)

#endif // STELLANE_ENABLE_LOG_MACROS

// ============================================================================
// 템플릿 구현
// ============================================================================

template<typename T>
Logger& Logger::with_field(const std::string& key, T value) {
std::lock_guard<std::mutex> lock(fields_mutex_);

```
if constexpr (std::is_arithmetic_v<T>) {
    fields_[key] = std::to_string(value);
} else if constexpr (std::is_same_v<T, std::string>) {
    fields_[key] = value;
} else if constexpr (std::is_convertible_v<T, std::string>) {
    fields_[key] = std::string(value);
} else {
    static_assert(false, "Type not supported for logging field");
}

return *this;
```

}

} // namespace stellane
