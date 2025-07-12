#include “stellane/utils/logger.h”
#include <iostream>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <regex>

namespace stellane {

// ============================================================================
// LogLevel 유틸리티 함수
// ============================================================================

std::string to_string(LogLevel level) {
switch (level) {
case LogLevel::TRACE: return “TRACE”;
case LogLevel::DEBUG: return “DEBUG”;
case LogLevel::INFO:  return “INFO”;
case LogLevel::WARN:  return “WARN”;
case LogLevel::ERROR: return “ERROR”;
case LogLevel::FATAL: return “FATAL”;
case LogLevel::OFF:   return “OFF”;
default: return “UNKNOWN”;
}
}

LogLevel from_string(const std::string& level_str) {
std::string upper_str = level_str;
std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);

```
if (upper_str == "TRACE") return LogLevel::TRACE;
if (upper_str == "DEBUG") return LogLevel::DEBUG;
if (upper_str == "INFO")  return LogLevel::INFO;
if (upper_str == "WARN" || upper_str == "WARNING") return LogLevel::WARN;
if (upper_str == "ERROR") return LogLevel::ERROR;
if (upper_str == "FATAL") return LogLevel::FATAL;
if (upper_str == "OFF")   return LogLevel::OFF;

return LogLevel::INFO; // 기본값
```

}

// ============================================================================
// LogMessage 구현
// ============================================================================

LogMessage::LogMessage(LogLevel lvl, std::string comp, std::string trace,
std::string msg, std::chrono::system_clock::time_point ts)
: level(lvl)
, component(std::move(comp))
, trace_id(std::move(trace))
, message(std::move(msg))
, timestamp(ts)
, thread_id(std::this_thread::get_id()) {
}

// ============================================================================
// TextFormatter 구현
// ============================================================================

TextFormatter::TextFormatter(bool include_thread_id)
: include_thread_id_(include_thread_id) {
}

std::string TextFormatter::format(const LogMessage& message) {
std::ostringstream oss;

```
// 타임스탬프 포맷팅 (ISO 8601)
auto time_t = std::chrono::system_clock::to_time_t(message.timestamp);
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    message.timestamp.time_since_epoch()) % 1000;

oss << "[" << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
    << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z] ";

// 로그 레벨
oss << "[" << std::setw(5) << std::left << to_string(message.level) << "] ";

// 컴포넌트
if (!message.component.empty()) {
    oss << "[" << message.component << "] ";
}

// Trace ID
if (!message.trace_id.empty()) {
    oss << "[trace_id=" << message.trace_id << "] ";
}

// 스레드 ID (선택적)
if (include_thread_id_) {
    oss << "[thread=" << message.thread_id << "] ";
}

// 메인 메시지
oss << message.message;

// 구조화된 필드
if (!message.fields.empty()) {
    oss << " {";
    bool first = true;
    for (const auto& [key, value] : message.fields) {
        if (!first) oss << ", ";
        oss << key << "=" << value;
        first = false;
    }
    oss << "}";
}

return oss.str();
```

}

// ============================================================================
// JsonFormatter 구현
// ============================================================================

JsonFormatter::JsonFormatter(bool pretty_print)
: pretty_print_(pretty_print) {
}

std::string JsonFormatter::format(const LogMessage& message) {
std::ostringstream oss;

```
if (pretty_print_) {
    oss << "{\n";
    oss << "  \"timestamp\": \"";
} else {
    oss << "{\"timestamp\":\"";
}

// 타임스탬프
auto time_t = std::chrono::system_clock::to_time_t(message.timestamp);
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    message.timestamp.time_since_epoch()) % 1000;

oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S")
    << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z\"";

// 로그 레벨
if (pretty_print_) {
    oss << ",\n  \"level\": \"" << to_string(message.level) << "\"";
} else {
    oss << ",\"level\":\"" << to_string(message.level) << "\"";
}

// 컴포넌트
if (!message.component.empty()) {
    if (pretty_print_) {
        oss << ",\n  \"component\": \"" << message.component << "\"";
    } else {
        oss << ",\"component\":\"" << message.component << "\"";
    }
}

// Trace ID
if (!message.trace_id.empty()) {
    if (pretty_print_) {
        oss << ",\n  \"trace_id\": \"" << message.trace_id << "\"";
    } else {
        oss << ",\"trace_id\":\"" << message.trace_id << "\"";
    }
}

// 스레드 ID
if (pretty_print_) {
    oss << ",\n  \"thread_id\": \"" << message.thread_id << "\"";
} else {
    oss << ",\"thread_id\":\"" << message.thread_id << "\"";
}

// 메시지 (JSON 이스케이프 처리)
std::string escaped_message = message.message;
std::replace(escaped_message.begin(), escaped_message.end(), '"', '\'');
std::replace(escaped_message.begin(), escaped_message.end(), '\n', ' ');

if (pretty_print_) {
    oss << ",\n  \"message\": \"" << escaped_message << "\"";
} else {
    oss << ",\"message\":\"" << escaped_message << "\"";
}

// 구조화된 필드
if (!message.fields.empty()) {
    if (pretty_print_) {
        oss << ",\n  \"fields\": {";
    } else {
        oss << ",\"fields\":{";
    }
    
    bool first = true;
    for (const auto& [key, value] : message.fields) {
        if (!first) oss << ",";
        
        if (pretty_print_) {
            oss << "\n    \"" << key << "\": \"" << value << "\"";
        } else {
            oss << "\"" << key << "\":\"" << value << "\"";
        }
        first = false;
    }
    
    if (pretty_print_) {
        oss << "\n  }";
    } else {
        oss << "}";
    }
}

if (pretty_print_) {
    oss << "\n}";
} else {
    oss << "}";
}

return oss.str();
```

}

// ============================================================================
// ConsoleSink 구현
// ============================================================================

ConsoleSink::ConsoleSink(bool use_colors)
: use_colors_(use_colors) {
}

void ConsoleSink::write(const std::string& formatted_message) {
std::lock_guard<std::mutex> lock(mutex_);
std::cout << formatted_message << std::endl;
}

void ConsoleSink::flush() {
std::lock_guard<std::mutex> lock(mutex_);
std::cout.flush();
}

std::string ConsoleSink::get_color_code(LogLevel level) const {
if (!use_colors_) return “”;

```
switch (level) {
    case LogLevel::TRACE: return "\033[37m";    // White
    case LogLevel::DEBUG: return "\033[36m";    // Cyan
    case LogLevel::INFO:  return "\033[32m";    // Green
    case LogLevel::WARN:  return "\033[33m";    // Yellow
    case LogLevel::ERROR: return "\033[31m";    // Red
    case LogLevel::FATAL: return "\033[35m";    // Magenta
    default: return "\033[0m";                  // Reset
}
```

}

// ============================================================================
// FileSink 구현
// ============================================================================

FileSink::FileSink(const std::string& filename, bool append)
: filename_(filename) {

```
// 디렉토리 생성
auto parent_path = std::filesystem::path(filename).parent_path();
if (!parent_path.empty()) {
    std::filesystem::create_directories(parent_path);
}

auto mode = append ? std::ios::out | std::ios::app : std::ios::out;
file_.open(filename_, mode);

if (!file_.is_open()) {
    throw std::runtime_error("Failed to open log file: " + filename_);
}

// 현재 파일 크기 확인
if (std::filesystem::exists(filename_)) {
    current_size_ = std::filesystem::file_size(filename_);
}
```

}

FileSink::~FileSink() {
if (file_.is_open()) {
file_.close();
}
}

void FileSink::write(const std::string& formatted_message) {
std::lock_guard<std::mutex> lock(mutex_);

```
if (!file_.is_open()) return;

file_ << formatted_message << "\n";
current_size_ += formatted_message.length() + 1;

if (rotation_enabled_) {
    rotate_if_needed();
}
```

}

void FileSink::flush() {
std::lock_guard<std::mutex> lock(mutex_);
if (file_.is_open()) {
file_.flush();
}
}

void FileSink::enable_rotation(size_t max_size_mb, size_t max_files) {
std::lock_guard<std::mutex> lock(mutex_);
rotation_enabled_ = true;
max_size_bytes_ = max_size_mb * 1024 * 1024;
max_files_ = max_files;
}

void FileSink::rotate_if_needed() {
if (current_size_ >= max_size_bytes_) {
perform_rotation();
}
}

void FileSink::perform_rotation() {
file_.close();

```
// 기존 로그 파일들 이름 변경
for (size_t i = max_files_ - 1; i > 0; --i) {
    std::string old_name = filename_ + "." + std::to_string(i);
    std::string new_name = filename_ + "." + std::to_string(i + 1);
    
    if (std::filesystem::exists(old_name)) {
        if (i == max_files_ - 1) {
            std::filesystem::remove(old_name);  // 가장 오래된 파일 삭제
        } else {
            std::filesystem::rename(old_name, new_name);
        }
    }
}

// 현재 파일을 .1로 이름 변경
if (std::filesystem::exists(filename_)) {
    std::filesystem::rename(filename_, filename_ + ".1");
}

// 새 파일 열기
file_.open(filename_, std::ios::out);
current_size_ = 0;
```

}

// ============================================================================
// AsyncSink 구현
// ============================================================================

AsyncSink::AsyncSink(std::shared_ptr<ILogSink> underlying_sink, size_t buffer_size)
: underlying_sink_(std::move(underlying_sink))
, buffer_size_(buffer_size)
, worker_thread_(&AsyncSink::worker_loop, this) {
}

AsyncSink::~AsyncSink() {
stop_flag_ = true;
queue_cv_.notify_all();

```
if (worker_thread_.joinable()) {
    worker_thread_.join();
}
```

}

void AsyncSink::write(const std::string& formatted_message) {
{
std::lock_guard<std::mutex> lock(queue_mutex_);

```
    // 버퍼가 가득 찬 경우 가장 오래된 메시지 제거 (백프레셔 방지)
    if (message_queue_.size() >= buffer_size_) {
        message_queue_.pop();
    }
    
    message_queue_.push(formatted_message);
}
queue_cv_.notify_one();
```

}

void AsyncSink::flush() {
// 비동기 싱크에서는 즉시 플러시 불가능
// 대신 현재 큐의 모든 메시지가 처리될 때까지 대기
while (pending_count() > 0) {
std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

```
if (underlying_sink_) {
    underlying_sink_->flush();
}
```

}

size_t AsyncSink::pending_count() const {
std::lock_guard<std::mutex> lock(queue_mutex_);
return message_queue_.size();
}

void AsyncSink::worker_loop() {
while (!stop_flag_) {
std::unique_lock<std::mutex> lock(queue_mutex_);

```
    // 메시지가 있거나 종료 신호가 올 때까지 대기
    queue_cv_.wait(lock, [this] { 
        return !message_queue_.empty() || stop_flag_; 
    });
    
    // 큐의 모든 메시지 처리
    while (!message_queue_.empty() && underlying_sink_) {
        std::string message = std::move(message_queue_.front());
        message_queue_.pop();
        
        lock.unlock();
        
        try {
            underlying_sink_->write(message);
        } catch (...) {
            // 로그 출력 실패 시 무시 (무한 루프 방지)
        }
        
        lock.lock();
    }
}

// 종료 시 남은 메시지들 처리
std::lock_guard<std::mutex> lock(queue_mutex_);
while (!message_queue_.empty() && underlying_sink_) {
    try {
        underlying_sink_->write(message_queue_.front());
        message_queue_.pop();
    } catch (...) {
        break;
    }
}
```

}

// ============================================================================
// Logger 구현
// ============================================================================

Logger::Logger(std::string component, std::string trace_id)
: component_(std::move(component))
, trace_id_(std::move(trace_id))
, formatter_(std::make_shared<TextFormatter>()) {

```
// 기본 콘솔 싱크 추가
add_sink(std::make_shared<ConsoleSink>());
```

}

Logger::Logger(const Logger& other)
: component_(other.component_)
, trace_id_(other.trace_id_)
, min_level_(other.min_level_.load())
, fields_(other.fields_)
, formatter_(other.formatter_) {

```
std::lock_guard<std::mutex> lock(other.sinks_mutex_);
sinks_ = other.sinks_;
```

}

Logger& Logger::operator=(const Logger& other) {
if (this != &other) {
component_ = other.component_;
trace_id_ = other.trace_id_;
min_level_ = other.min_level_.load();

```
    {
        std::lock_guard<std::mutex> lock1(fields_mutex_);
        std::lock_guard<std::mutex> lock2(other.fields_mutex_);
        fields_ = other.fields_;
    }
    
    formatter_ = other.formatter_;
    
    {
        std::lock_guard<std::mutex> lock1(sinks_mutex_);
        std::lock_guard<std::mutex> lock2(other.sinks_mutex_);
        sinks_ = other.sinks_;
    }
}
return *this;
```

}

Logger::Logger(Logger&& other) noexcept
: component_(std::move(other.component_))
, trace_id_(std::move(other.trace_id_))
, min_level_(other.min_level_.load())
, fields_(std::move(other.fields_))
, sinks_(std::move(other.sinks_))
, formatter_(std::move(other.formatter_)) {
}

Logger& Logger::operator=(Logger&& other) noexcept {
if (this != &other) {
component_ = std::move(other.component_);
trace_id_ = std::move(other.trace_id_);
min_level_ = other.min_level_.load();
fields_ = std::move(other.fields_);
sinks_ = std::move(other.sinks_);
formatter_ = std::move(other.formatter_);
}
return *this;
}

Logger::~Logger() {
flush();
}

void Logger::trace(const std::string& message) {
log(LogLevel::TRACE, message);
}

void Logger::debug(const std::string& message) {
log(LogLevel::DEBUG, message);
}

void Logger::info(const std::string& message) {
log(LogLevel::INFO, message);
}

void Logger::warn(const std::string& message) {
log(LogLevel::WARN, message);
}

void Logger::error(const std::string& message) {
log(LogLevel::ERROR, message);
}

void Logger::fatal(const std::string& message) {
log(LogLevel::FATAL, message);
}

void Logger::log(LogLevel level, const std::string& message) {
if (!is_enabled(level)) {
return;
}

```
do_log(level, message);
```

}

Logger& Logger::with_field(const std::string& key, const std::string& value) {
std::lock_guard<std::mutex> lock(fields_mutex_);
fields_[key] = value;
return *this;
}

Logger& Logger::clear_fields() {
std::lock_guard<std::mutex> lock(fields_mutex_);
fields_.clear();
return *this;
}

void Logger::set_level(LogLevel level) {
min_level_ = level;
}

LogLevel Logger::get_level() const {
return min_level_.load();
}

bool Logger::is_enabled(LogLevel level) const {
return level >= min_level_.load();
}

void Logger::add_sink(std::shared_ptr<ILogSink> sink) {
if (!sink) return;

```
std::lock_guard<std::mutex> lock(sinks_mutex_);
sinks_.push_back(std::move(sink));
```

}

void Logger::clear_sinks() {
std::lock_guard<std::mutex> lock(sinks_mutex_);
sinks_.clear();
}

void Logger::set_formatter(std::shared_ptr<ILogFormatter> formatter) {
if (formatter) {
formatter_ = std::move(formatter);
}
}

void Logger::flush() {
std::lock_guard<std::mutex> lock(sinks_mutex_);
for (auto& sink : sinks_) {
if (sink) {
sink->flush();
}
}
}

const std::string& Logger::component() const {
return component_;
}

const std::string& Logger::trace_id() const {
return trace_id_;
}

void Logger::set_trace_id(const std::string& new_trace_id) {
trace_id_ = new_trace_id;
}

void Logger::do_log(LogLevel level, const std::string& message) {
// 로그 메시지 생성
LogMessage log_msg(level, component_, trace_id_, message);

```
// 구조화된 필드 복사
{
    std::lock_guard<std::mutex> lock(fields_mutex_);
    log_msg.fields = fields_;
}

// 포맷팅
std::string formatted_message;
if (formatter_) {
    formatted_message = formatter_->format(log_msg);
} else {
    formatted_message = message;
}

// 모든 싱크에 출력
std::lock_guard<std::mutex> lock(sinks_mutex_);
for (auto& sink : sinks_) {
    if (sink) {
        sink->write(formatted_message);
    }
}
```

}

// ============================================================================
// LoggerFactory 구현
// ============================================================================

std::unordered_map<std::string, std::unique_ptr<Logger>> LoggerFactory::loggers_;
std::mutex LoggerFactory::loggers_mutex_;
std::shared_ptr<ILogSink> LoggerFactory::default_sink_;
std::shared_ptr<ILogFormatter> LoggerFactory::default_formatter_;
LogLevel LoggerFactory::global_level_ = LogLevel::INFO;

Logger& LoggerFactory::get_default() {
return get_logger(“stellane”);
}

Logger& LoggerFactory::get_logger(const std::string& component) {
std::lock_guard<std::mutex> lock(loggers_mutex_);

```
auto it = loggers_.find(component);
if (it != loggers_.end()) {
    return *it->second;
}

// 새 로거 생성
auto logger = std::make_unique<Logger>(component);
logger->set_level(global_level_);

if (default_sink_) {
    logger->clear_sinks();
    logger->add_sink(default_sink_);
}

if (default_formatter_) {
    logger->set_formatter(default_formatter_);
}

Logger& logger_ref = *logger;
loggers_[component] = std::move(logger);

return logger_ref;
```

}

void LoggerFactory::set_global_level(LogLevel level) {
std::lock_guard<std::mutex> lock(loggers_mutex_);
global_level_ = level;

```
// 기존 로거들에도 적용
for (auto& [name, logger] : loggers_) {
    logger->set_level(level);
}
```

}

void LoggerFactory::set_default_sink(std::shared_ptr<ILogSink> sink) {
std::lock_guard<std::mutex> lock(loggers_mutex_);
default_sink_ = std::move(sink);
}

void LoggerFactory::set_default_formatter(std::shared_ptr<ILogFormatter> formatter) {
std::lock_guard<std::mutex> lock(loggers_mutex_);
default_formatter_ = std::move(formatter);
}

void LoggerFactory::flush_all() {
std::lock_guard<std::mutex> lock(loggers_mutex_);
for (auto& [name, logger] : loggers_) {
logger->flush();
}
}

} // namespace stellane
