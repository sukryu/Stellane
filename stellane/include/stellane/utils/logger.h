#pragma once

#include <string>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <fstream>
#include <cassert>

namespace stellane {

// ============================================================================
// 락 계층 구조 (Lock Hierarchy) - 데드락 방지
// ============================================================================

/**

- @brief 락 레벨 정의 - 낮은 번호부터 높은 번호 순서로 획득
- 
- 규칙:
- 1. 항상 낮은 레벨 → 높은 레벨 순서로 락 획득
- 1. 같은 레벨의 락은 동시에 획득하지 않음
- 1. 디버그 모드에서 위반 시 assertion 발생
   */
   enum class LockLevel : int {
   GLOBAL_CONFIG = 0,    ///< 전역 설정 (LoggerFactory)
   LOGGER_REGISTRY = 1,  ///< 로거 레지스트리 관리
   LOGGER_CONFIG = 2,    ///< 개별 로거 설정 (sinks, formatter)
   LOGGER_FIELDS = 3,    ///< 구조화된 필드 수정
   SINK_INTERNAL = 4     ///< 싱크 내부 상태 (파일 I/O 등)
   };

/**

- @brief 계층적 뮤텍스 - 락 순서 검증 기능 포함
  */
  class HierarchicalMutex {
  public:
  explicit HierarchicalMutex(LockLevel level) : level_(level) {}
  
  void lock() {
  check_lock_order();
  mutex_.lock();
  update_thread_level();
  }
  
  void unlock() {
  reset_thread_level();
  mutex_.unlock();
  }
  
  bool try_lock() {
  if (!check_lock_order_noassert()) return false;
  if (mutex_.try_lock()) {
  update_thread_level();
  return true;
  }
  return false;
  }
  
  LockLevel level() const noexcept { return level_; }

private:
std::mutex mutex_;
LockLevel level_;

```
// 스레드별 현재 락 레벨 추적
static thread_local LockLevel current_thread_level_;

void check_lock_order() {
```

#ifdef DEBUG
assert(static_cast<int>(level_) > static_cast<int>(current_thread_level_) &&
“Lock hierarchy violation detected!”);
#endif
}

```
bool check_lock_order_noassert() const noexcept {
    return static_cast<int>(level_) > static_cast<int>(current_thread_level_);
}

void update_thread_level() {
    current_thread_level_ = level_;
}

void reset_thread_level() {
    current_thread_level_ = static_cast<LockLevel>(static_cast<int>(level_) - 1);
}
```

};

/**

- @brief 계층적 뮤텍스용 RAII 락 가드
  */
  class HierarchicalLockGuard {
  public:
  explicit HierarchicalLockGuard(HierarchicalMutex& mutex) : mutex_(mutex) {
  mutex_.lock();
  }
  
  ~HierarchicalLockGuard() {
  mutex_.unlock();
  }
  
  HierarchicalLockGuard(const HierarchicalLockGuard&) = delete;
  HierarchicalLockGuard& operator=(const HierarchicalLockGuard&) = delete;

private:
HierarchicalMutex& mutex_;
};

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

[[nodiscard]] std::string to_string(LogLevel level);
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
virtual std::string format(const LogMessage& message) = 0;
};

class TextFormatter : public ILogFormatter {
public:
explicit TextFormatter(bool include_thread_id = false);
std::string format(const LogMessage& message) override;

private:
bool include_thread_id_;
};

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
virtual void write(const std::string& formatted_message) = 0;
virtual void flush() = 0;
};

class ConsoleSink : public ILogSink {
public:
explicit ConsoleSink(bool use_colors = true);
void write(const std::string& formatted_message) override;
void flush() override;

private:
bool use_colors_;
HierarchicalMutex mutex_{LockLevel::SINK_INTERNAL};

```
std::string get_color_code(LogLevel level) const;
```

};

class FileSink : public ILogSink {
public:
explicit FileSink(const std::string& filename, bool append = true);
~FileSink();

```
void write(const std::string& formatted_message) override;
void flush() override;
void enable_rotation(size_t max_size_mb = 100, size_t max_files = 10);
```

private:
std::string filename_;
std::ofstream file_;
HierarchicalMutex mutex_{LockLevel::SINK_INTERNAL};

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

class AsyncSink : public ILogSink {
public:
explicit AsyncSink(std::shared_ptr<ILogSink> underlying_sink,
size_t buffer_size = 1000);
~AsyncSink();

```
void write(const std::string& formatted_message) override;
void flush() override;
[[nodiscard]] size_t pending_count() const;
```

private:
std::shared_ptr<ILogSink> underlying_sink_;

```
// 비동기 처리 - 별도 스레드에서 실행되므로 독립적인 락 사용
std::queue<std::string> message_queue_;
mutable std::mutex queue_mutex_;  // 표준 뮤텍스 사용 (스레드 간 통신)
std::condition_variable queue_cv_;
std::thread worker_thread_;
std::atomic<bool> stop_flag_{false};
size_t buffer_size_;

void worker_loop();
```

};

// ============================================================================
// 메인 Logger 클래스 (개선된 버전)
// ============================================================================

/**

- @brief 데드락 방지 Logger - 계층적 락 구조 적용
- 
- 락 순서:
- 1. config_mutex_ (LOGGER_CONFIG) - 싱크/포매터 설정
- 1. fields_mutex_ (LOGGER_FIELDS) - 구조화된 필드
- 
- 주의: do_log()에서는 읽기 전용 접근만 수행하여 락 충돌 최소화
  */
  class Logger {
  public:
  explicit Logger(std::string component, std::string trace_id = “”);
  
  // 복사/이동 생성자 - 락 순서 유지
  Logger(const Logger& other);
  Logger& operator=(const Logger& other);
  Logger(Logger&& other) noexcept;
  Logger& operator=(Logger&& other) noexcept;
  
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
  void log(LogLevel level, const std::string& message);
  
  // ========================================================================
  // 구조화된 로깅 (Fluent Interface)
  // ========================================================================
  
  Logger& with_field(const std::string& key, const std::string& value);
  
  template<typename T>
  Logger& with_field(const std::string& key, T value);
  
  Logger& clear_fields();
  
  // ========================================================================
  // 설정 메서드 - 계층적 락 적용
  // ========================================================================
  
  void set_level(LogLevel level);
  [[nodiscard]] LogLevel get_level() const;
  [[nodiscard]] bool is_enabled(LogLevel level) const;
  
  /**
  - @brief 싱크 추가 - LOGGER_CONFIG 레벨 락 사용
    */
    void add_sink(std::shared_ptr<ILogSink> sink);
    void clear_sinks();
    void set_formatter(std::shared_ptr<ILogFormatter> formatter);
    void flush();
  
  // ========================================================================
  // 메타데이터 접근
  // ========================================================================
  
  [[nodiscard]] const std::string& component() const;
  [[nodiscard]] const std::string& trace_id() const;
  void set_trace_id(const std::string& new_trace_id);

private:
// 불변 데이터 (락 불필요)
std::string component_;
std::string trace_id_;
std::atomic<LogLevel> min_level_{LogLevel::INFO};

```
// 구조화된 필드 - LOGGER_FIELDS 레벨
std::unordered_map<std::string, std::string> fields_;
HierarchicalMutex fields_mutex_{LockLevel::LOGGER_FIELDS};

// 설정 데이터 - LOGGER_CONFIG 레벨
std::vector<std::shared_ptr<ILogSink>> sinks_;
std::shared_ptr<ILogFormatter> formatter_;
HierarchicalMutex config_mutex_{LockLevel::LOGGER_CONFIG};

/**
 * @brief 실제 로그 출력 - 락 순서 최적화
 * 
 * 1. fields 스냅샷 생성 (짧은 락)
 * 2. config 스냅샷 생성 (짧은 락) 
 * 3. 락 해제 후 실제 출력 수행
 */
void do_log(LogLevel level, const std::string& message);

/**
 * @brief 안전한 복사 구현 - 락 순서 보장
 */
void safe_copy_from(const Logger& other);
```

};

// ============================================================================
// 전역 로거 관리 - 계층적 락 적용
// ============================================================================

class LoggerFactory {
public:
static Logger& get_default();
static Logger& get_logger(const std::string& component);

```
// 전역 설정 - GLOBAL_CONFIG 레벨 락
static void set_global_level(LogLevel level);
static void set_default_sink(std::shared_ptr<ILogSink> sink);
static void set_default_formatter(std::shared_ptr<ILogFormatter> formatter);
static void flush_all();
```

private:
// 레지스트리 관리 - LOGGER_REGISTRY 레벨
static std::unordered_map<std::string, std::unique_ptr<Logger>> loggers_;
static HierarchicalMutex registry_mutex_;

```
// 기본 설정 - GLOBAL_CONFIG 레벨
static std::shared_ptr<ILogSink> default_sink_;
static std::shared_ptr<ILogFormatter> default_formatter_;
static std::atomic<LogLevel> global_level_;
static HierarchicalMutex config_mutex_;
```

};

// ============================================================================
// 템플릿 구현
// ============================================================================

template<typename T>
Logger& Logger::with_field(const std::string& key, T value) {
HierarchicalLockGuard lock(fields_mutex_);

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

#endif // STELLANE_ENABLE_LOG_MACROS

} // namespace stellane
