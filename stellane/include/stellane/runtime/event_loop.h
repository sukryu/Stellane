#pragma once

#include “stellane/runtime/runtime_config.h”
#include “stellane/core/task.h”
#include “stellane/utils/logger.h”

#include <memory>
#include <functional>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <string>
#include <exception>
#include <future>
#include <optional>

namespace stellane {

// Forward declarations
class Context;
class Request;
class Response;

// ============================================================================
// Event Loop Core Types
// ============================================================================

/**

- @brief Event loop execution statistics
  */
  struct EventLoopStats {
  std::chrono::steady_clock::time_point start_time;     ///< Loop start timestamp
  std::chrono::milliseconds uptime;                     ///< Total uptime
  size_t total_iterations = 0;                          ///< Total loop iterations
  size_t total_tasks_processed = 0;                     ///< Total tasks executed
  size_t total_io_events = 0;                           ///< Total I/O events handled
  size_t current_pending_tasks = 0;                     ///< Currently pending tasks
  size_t peak_pending_tasks = 0;                        ///< Peak pending task count
  double average_iteration_time_us = 0.0;               ///< Average iteration time (microseconds)
  double cpu_usage_percent = 0.0;                       ///< CPU usage percentage
  
  /**
  - @brief Calculate tasks per second
  - @return Tasks processed per second
    */
    [[nodiscard]] double tasks_per_second() const noexcept {
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    return uptime_seconds > 0 ? static_cast<double>(total_tasks_processed) / uptime_seconds : 0.0;
    }
  
  /**
  - @brief Get formatted statistics string
  - @return Human-readable statistics
    */
    [[nodiscard]] std::string to_string() const;
    };

/**

- @brief I/O operation types for event handling
  */
  enum class IoOperationType : int {
  READ = 1,         ///< Read operation
  WRITE = 2,        ///< Write operation
  ACCEPT = 4,       ///< Accept new connection
  CONNECT = 8,      ///< Establish connection
  CLOSE = 16,       ///< Close connection
  TIMEOUT = 32      ///< Timeout event
  };

/**

- @brief I/O event descriptor
  */
  struct IoEvent {
  int fd;                           ///< File descriptor
  IoOperationType type;             ///< Operation type
  size_t bytes_transferred = 0;     ///< Bytes read/written
  int error_code = 0;               ///< Error code (0 = success)
  void* user_data = nullptr;        ///< User-defined data
  
  /**
  - @brief Check if event indicates an error
  - @return true if error occurred
    */
    [[nodiscard]] bool is_error() const noexcept {
    return error_code != 0;
    }
  
  /**
  - @brief Check if event is successful
  - @return true if operation completed successfully
    */
    [[nodiscard]] bool is_success() const noexcept {
    return error_code == 0;
    }
    };

/**

- @brief I/O event handler function signature
  */
  using IoEventHandler = std::function<void(const IoEvent&)>;

/**

- @brief Timer callback function signature
  */
  using TimerCallback = std::function<void()>;

/**

- @brief Timer handle for managing scheduled timers
  */
  class TimerHandle {
  public:
  TimerHandle() = default;
  explicit TimerHandle(size_t id) : id_(id), valid_(true) {}
  
  /**
  - @brief Cancel the timer
    */
    void cancel() { valid_ = false; }
  
  /**
  - @brief Check if timer is still valid
  - @return true if timer is active
    */
    [[nodiscard]] bool is_valid() const noexcept { return valid_; }
  
  /**
  - @brief Get timer ID
  - @return Timer identifier
    */
    [[nodiscard]] size_t id() const noexcept { return id_; }

private:
size_t id_ = 0;
bool valid_ = false;
};

// ============================================================================
// Event Loop Backend Interface
// ============================================================================

/**

- @brief Abstract interface for event loop backend implementations
- 
- This interface defines the contract for all event loop backends in Stellane.
- Implementations must provide efficient task scheduling, I/O event handling,
- and timer management capabilities.
- 
- **Thread Safety**: All methods must be thread-safe unless explicitly noted.
- **Performance**: Implementations should optimize for minimal latency and
- high throughput, especially for real-time applications like chat systems.
- 
- **Available Implementations**:
- - LibUVBackend: Cross-platform using libuv
- - EpollBackend: Linux-specific using epoll
- - IoUringBackend: Linux io_uring for ultra-high performance
- - StellaneBackend: Custom multi-threaded implementation
    */
    class IEventLoopBackend {
    public:
    virtual ~IEventLoopBackend() = default;
  
  // ========================================================================
  // Lifecycle Management
  // ========================================================================
  
  /**
  - @brief Start the event loop (blocking operation)
  - 
  - This method blocks the calling thread and begins processing events.
  - It returns only when stop() is called or a fatal error occurs.
  - 
  - @throws EventLoopException if loop cannot be started
    */
    virtual void run() = 0;
  
  /**
  - @brief Stop the event loop gracefully
  - 
  - Initiates graceful shutdown of the event loop. Currently executing
  - tasks are allowed to complete, but no new tasks are accepted.
  - 
  - **Thread Safety**: This method is thread-safe and can be called
  - from any thread to stop the loop running in another thread.
    */
    virtual void stop() = 0;
  
  /**
  - @brief Check if the event loop is currently running
  - @return true if the loop is active and processing events
    */
    virtual bool is_running() const = 0;
  
  /**
  - @brief Get the number of pending tasks in the queue
  - @return Number of tasks waiting to be executed
    */
    virtual size_t pending_tasks() const = 0;
  
  // ========================================================================
  // Task Scheduling
  // ========================================================================
  
  /**
  - @brief Schedule a task for execution
  - @param task Task to be scheduled
  - 
  - Adds the task to the execution queue. The task will be executed
  - in the next iteration of the event loop.
  - 
  - **Thread Safety**: This method is thread-safe and can be called
  - from any thread to schedule tasks on the event loop.
    */
    virtual void schedule(Task<> task) = 0;
  
  /**
  - @brief Schedule a task with specific priority
  - @param task Task to be scheduled
  - @param priority Task priority (higher values = higher priority)
  - 
  - Only effective if the backend supports priority-based scheduling.
  - Default implementations may ignore the priority parameter.
    */
    virtual void schedule_with_priority(Task<> task, int priority) {
    // Default implementation ignores priority
    schedule(std::move(task));
    }
  
  /**
  - @brief Schedule a task to run after a delay
  - @param task Task to execute
  - @param delay Delay before execution
  - @return Timer handle to cancel the delayed task
    */
    virtual TimerHandle schedule_delayed(Task<> task, std::chrono::milliseconds delay) = 0;
  
  /**
  - @brief Schedule a periodic task
  - @param task Task to execute repeatedly
  - @param interval Execution interval
  - @return Timer handle to cancel the periodic task
    */
    virtual TimerHandle schedule_periodic(Task<> task, std::chrono::milliseconds interval) = 0;
  
  // ========================================================================
  // I/O Operations
  // ========================================================================
  
  /**
  - @brief Register interest in I/O events for a file descriptor
  - @param fd File descriptor to monitor
  - @param events Bitmask of IoOperationType values
  - @param handler Callback for when events occur
  - @return true if registration successful
    */
    virtual bool register_io(int fd, int events, IoEventHandler handler) = 0;
  
  /**
  - @brief Unregister I/O monitoring for a file descriptor
  - @param fd File descriptor to stop monitoring
  - @return true if unregistration successful
    */
    virtual bool unregister_io(int fd) = 0;
  
  /**
  - @brief Modify I/O event interest for a file descriptor
  - @param fd File descriptor
  - @param events New event mask
  - @return true if modification successful
    */
    virtual bool modify_io(int fd, int events) = 0;
  
  // ========================================================================
  // Timer Management
  // ========================================================================
  
  /**
  - @brief Create a one-shot timer
  - @param delay Delay before firing
  - @param callback Function to call when timer fires
  - @return Timer handle
    */
    virtual TimerHandle create_timer(std::chrono::milliseconds delay, TimerCallback callback) = 0;
  
  /**
  - @brief Create a repeating timer
  - @param interval Repeat interval
  - @param callback Function to call on each interval
  - @return Timer handle
    */
    virtual TimerHandle create_repeating_timer(std::chrono::milliseconds interval, TimerCallback callback) = 0;
  
  /**
  - @brief Cancel a timer
  - @param handle Timer handle to cancel
  - @return true if timer was successfully cancelled
    */
    virtual bool cancel_timer(const TimerHandle& handle) = 0;
  
  // ========================================================================
  // Statistics and Monitoring
  // ========================================================================
  
  /**
  - @brief Get event loop performance statistics
  - @return Current performance statistics
    */
    virtual EventLoopStats get_stats() const = 0;
  
  /**
  - @brief Reset performance statistics
    */
    virtual void reset_stats() = 0;
  
  /**
  - @brief Enable or disable detailed performance profiling
  - @param enabled Whether to enable profiling
  - 
  - When enabled, collects detailed timing information at the cost
  - of some performance overhead.
    */
    virtual void enable_profiling(bool enabled) = 0;
  
  // ========================================================================
  // Configuration and Tuning
  // ========================================================================
  
  /**
  - @brief Get backend-specific information
  - @return Backend information structure
    */
    virtual BackendInfo get_backend_info() const = 0;
  
  /**
  - @brief Update backend configuration (hot reload)
  - @param config New configuration
  - @return true if configuration was successfully applied
  - 
  - Only a subset of configuration options may support hot reloading.
    */
    virtual bool update_config(const RuntimeConfig& config) = 0;
  
  /**
  - @brief Get current configuration
  - @return Current backend configuration
    */
    virtual RuntimeConfig get_config() const = 0;
  
  // ========================================================================
  // Advanced Features
  // ========================================================================
  
  /**
  - @brief Execute a function synchronously within the event loop
  - @param func Function to execute
  - @return Future for the execution result
  - 
  - Executes the function in the event loop thread and returns the result.
  - Useful for thread-safe operations that must run in the loop context.
    */
    template<typename F>
    std::future<std::invoke_result_t<F>> execute_sync(F&& func);
  
  /**
  - @brief Post a function to be executed in the next event loop iteration
  - @param func Function to execute
  - 
  - Similar to schedule() but for non-coroutine functions.
    */
    virtual void post(std::function<void()> func) = 0;
  
  /**
  - @brief Get the thread ID of the event loop thread
  - @return Thread ID
    */
    virtual std::thread::id get_thread_id() const = 0;
  
  /**
  - @brief Check if current thread is the event loop thread
  - @return true if called from within the event loop thread
    */
    virtual bool is_in_loop_thread() const = 0;
    };

// ============================================================================
// Event Loop Factory and Registry
// ============================================================================

/**

- @brief Factory for creating event loop backends
  */
  class EventLoopFactory {
  public:
  /**
  - @brief Backend creation function signature
    */
    using BackendFactory = std::function<std::unique_ptr<IEventLoopBackend>(const RuntimeConfig&)>;
  
  /**
  - @brief Create an event loop backend of the specified type
  - @param backend Backend type to create
  - @param config Configuration for the backend
  - @return Unique pointer to the created backend
  - @throws EventLoopException if backend creation fails
    */
    static std::unique_ptr<IEventLoopBackend> create(RuntimeBackend backend, const RuntimeConfig& config);
  
  /**
  - @brief Register a custom backend factory
  - @param name Backend name
  - @param factory Backend creation function
    */
    static void register_backend(const std::string& name, BackendFactory factory);
  
  /**
  - @brief Get list of available backend names
  - @return Vector of registered backend names
    */
    static std::vector<std::string> get_available_backends();
  
  /**
  - @brief Check if a specific backend is available
  - @param backend Backend type to check
  - @return true if backend is available on current platform
    */
    static bool is_backend_available(RuntimeBackend backend);
  
  /**
  - @brief Get the recommended backend for current platform
  - @return Recommended backend type
    */
    static RuntimeBackend get_recommended_backend();

private:
static std::unordered_map<std::string, BackendFactory> factories_;
static std::mutex factories_mutex_;
};

// ============================================================================
// Event Loop Exceptions
// ============================================================================

/**

- @brief Base exception for event loop related errors
  */
  class EventLoopException : public std::exception {
  public:
  explicit EventLoopException(const std::string& message) : message_(message) {}
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

/**

- @brief Exception thrown when backend initialization fails
  */
  class BackendInitializationException : public EventLoopException {
  public:
  BackendInitializationException(const std::string& backend, const std::string& reason)
  : EventLoopException(“Failed to initialize “ + backend + “ backend: “ + reason) {}
  };

/**

- @brief Exception thrown when I/O operations fail
  */
  class IoOperationException : public EventLoopException {
  public:
  IoOperationException(const std::string& operation, int error_code)
  : EventLoopException(“I/O operation ‘” + operation + “’ failed with error: “ + std::to_string(error_code)) {}
  };

/**

- @brief Exception thrown when timer operations fail
  */
  class TimerException : public EventLoopException {
  public:
  explicit TimerException(const std::string& message)
  : EventLoopException(“Timer operation failed: “ + message) {}
  };

// ============================================================================
// Base Event Loop Implementation
// ============================================================================

/**

- @brief Base implementation providing common functionality for event loop backends
- 
- This class provides default implementations for common operations and
- utility methods that most backends can reuse. Concrete backends should
- inherit from this class and override the pure virtual methods.
  */
  class BaseEventLoop : public IEventLoopBackend {
  public:
  explicit BaseEventLoop(const RuntimeConfig& config);
  virtual ~BaseEventLoop();
  
  // IEventLoopBackend interface - common implementations
  bool is_running() const override;
  size_t pending_tasks() const override;
  EventLoopStats get_stats() const override;
  void reset_stats() override;
  void enable_profiling(bool enabled) override;
  RuntimeConfig get_config() const override;
  bool update_config(const RuntimeConfig& config) override;
  std::thread::id get_thread_id() const override;
  bool is_in_loop_thread() const override;
  
  // Common timer management
  TimerHandle create_timer(std::chrono::milliseconds delay, TimerCallback callback) override;
  TimerHandle create_repeating_timer(std::chrono::milliseconds interval, TimerCallback callback) override;
  bool cancel_timer(const TimerHandle& handle) override;
  
  // Task scheduling with priority queue support
  void schedule_with_priority(Task<> task, int priority) override;
  TimerHandle schedule_delayed(Task<> task, std::chrono::milliseconds delay) override;
  TimerHandle schedule_periodic(Task<> task, std::chrono::milliseconds interval) override;
  
  // Function posting
  void post(std::function<void()> func) override;

protected:
// Protected interface for derived classes

```
/**
 * @brief Process pending tasks in the task queue
 * @param max_tasks Maximum number of tasks to process (0 = unlimited)
 * @return Number of tasks processed
 */
size_t process_tasks(size_t max_tasks = 0);

/**
 * @brief Process expired timers
 * @return Number of timers processed
 */
size_t process_timers();

/**
 * @brief Update performance statistics
 */
void update_stats();

/**
 * @brief Get the logger instance
 * @return Reference to the event loop logger
 */
Logger& logger() { return *logger_; }

/**
 * @brief Check if shutdown has been requested
 * @return true if stop() has been called
 */
bool should_stop() const { return stop_requested_.load(); }

/**
 * @brief Mark the event loop as started
 */
void mark_started();

/**
 * @brief Mark the event loop as stopped
 */
void mark_stopped();
```

private:
// Configuration and state
RuntimeConfig config_;
std::shared_ptr<Logger> logger_;
std::atomic<bool> running_{false};
std::atomic<bool> stop_requested_{false};
std::thread::id loop_thread_id_;

```
// Task queue management
struct PriorityTask {
    Task<> task;
    int priority;
    std::chrono::steady_clock::time_point created_at;
    
    bool operator<(const PriorityTask& other) const {
        return priority < other.priority; // Higher priority = lower value in priority_queue
    }
};

std::priority_queue<PriorityTask> task_queue_;
mutable std::mutex task_queue_mutex_;
std::condition_variable task_queue_cv_;

// Timer management
struct Timer {
    size_t id;
    std::chrono::steady_clock::time_point fire_time;
    std::chrono::milliseconds interval; // 0 for one-shot timers
    TimerCallback callback;
    bool active = true;
    
    bool operator>(const Timer& other) const {
        return fire_time > other.fire_time; // Earlier time = higher priority
    }
};

std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timer_queue_;
std::atomic<size_t> next_timer_id_{1};
mutable std::mutex timer_mutex_;

// Function queue for non-coroutine functions
std::queue<std::function<void()>> function_queue_;
mutable std::mutex function_queue_mutex_;

// Statistics
mutable EventLoopStats stats_;
mutable std::mutex stats_mutex_;
bool profiling_enabled_ = false;
std::chrono::steady_clock::time_point last_stats_update_;

// Helper methods
void initialize_logger();
void cleanup_expired_timers();
```

};

// ============================================================================
// Template Method Implementations
// ============================================================================

template<typename F>
std::future<std::invoke_result_t<F>> IEventLoopBackend::execute_sync(F&& func) {
using ReturnType = std::invoke_result_t<F>;
auto promise = std::make_shared<std::promise<ReturnType>>();
auto future = promise->get_future();

```
post([promise, func = std::forward<F>(func)]() {
    try {
        if constexpr (std::is_void_v<ReturnType>) {
            func();
            promise->set_value();
        } else {
            auto result = func();
            promise->set_value(std::move(result));
        }
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
});

return future;
```

}

} // namespace stellane
