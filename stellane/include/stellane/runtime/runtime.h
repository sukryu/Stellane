#pragma once

#include “stellane/runtime/runtime_config.h”
#include “stellane/runtime/runtime_stats.h”
#include “stellane/runtime/event_loop.h”
#include “stellane/runtime/task_scheduler.h”
#include “stellane/core/task.h”
#include “stellane/core/context.h”
#include “stellane/http/request.h”
#include “stellane/http/response.h”

#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <chrono>
#include <future>
#include <exception>

namespace stellane {

// Forward declarations
class IEventLoopBackend;
class ITaskScheduler;
class RequestJournal;
class Logger;

// ============================================================================
// Runtime Exception Types
// ============================================================================

/**

- @brief Base exception for runtime-related errors
  */
  class RuntimeException : public std::exception {
  public:
  explicit RuntimeException(const std::string& message) : message_(message) {}
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

/**

- @brief Exception thrown when runtime initialization fails
  */
  class RuntimeInitializationException : public RuntimeException {
  public:
  explicit RuntimeInitializationException(const std::string& message)
  : RuntimeException(“Runtime initialization failed: “ + message) {}
  };

/**

- @brief Exception thrown when task execution fails
  */
  class TaskExecutionException : public RuntimeException {
  public:
  TaskExecutionException(const std::string& task_name, const std::string& error)
  : RuntimeException(“Task ‘” + task_name + “’ failed: “ + error) {}
  };

/**

- @brief Exception thrown when backend operations fail
  */
  class BackendException : public RuntimeException {
  public:
  BackendException(const std::string& backend, const std::string& error)
  : RuntimeException(“Backend ‘” + backend + “’ error: “ + error) {}
  };

// ============================================================================
// Recovery Hook Types
// ============================================================================

/**

- @brief Recovery hook function signature
- 
- Called when a request needs to be recovered after a failure.
- The hook should determine whether to retry the request and how.
  */
  using RecoveryHook = std::function<Task<>(Context&, const Request&)>;

/**

- @brief Advanced recovery hook with metadata
- 
- Provides additional recovery context including attempt count and failure reason.
  */
  using AdvancedRecoveryHook = std::function<Task<>(Context&, const Request&, const RecoveryMetadata&)>;

/**

- @brief Task error handler function signature
- 
- Called when a task execution results in an unhandled exception.
  */
  using TaskErrorHandler = std::function<void(const std::exception&, const Task<>&)>;

// ============================================================================
// Main Runtime Class
// ============================================================================

/**

- @brief Stellane’s core asynchronous runtime engine
- 
- The Runtime class is the central orchestrator for all asynchronous operations
- in Stellane. It manages event loops, task scheduling, and provides high-performance
- execution of coroutines with pluggable backend support.
- 
- Key features:
- - **Pluggable Backends**: Support for libuv, epoll, io_uring, and custom backends
- - **Advanced Scheduling**: FIFO, priority-based, work-stealing strategies
- - **Request Recovery**: Fault-tolerant request processing with journaling
- - **Performance Monitoring**: Real-time statistics and profiling
- - **Thread Safety**: All public methods are thread-safe
- 
- @example
- ```cpp
  
  ```
- // Basic initialization
- RuntimeConfig config = RuntimeConfig::production();
- Runtime::init(config);
- 
- // Schedule tasks
- Runtime::schedule(my_async_task());
- Runtime::schedule_with_priority(urgent_task(), 10);
- 
- // Start runtime (blocks until shutdown)
- Runtime::start();
- ```
  
  ```
- 
- @example
- ```cpp
  
  ```
- // Advanced usage with recovery
- Runtime::enable_request_recovery();
- Runtime::on_recover([](Context& ctx, const Request& req) -> Task<> {
- ```
  ctx.logger().info("Recovering request: {}", req.path());
  ```
- ```
  co_await retry_original_handler(req);
  ```
- });
- ```
  
  ```

*/
class Runtime {
public:
// ========================================================================
// Lifecycle Management
// ========================================================================

```
/**
 * @brief Initialize the runtime with specified configuration
 * @param config Runtime configuration
 * @throws RuntimeInitializationException if initialization fails
 * 
 * This method must be called before any other runtime operations.
 * It initializes the selected backend, creates worker threads,
 * and prepares the task scheduling system.
 */
static void init(const RuntimeConfig& config);

/**
 * @brief Initialize runtime from TOML configuration file
 * @param config_path Path to TOML configuration file
 * @throws RuntimeInitializationException if file parsing or initialization fails
 */
static void init_from_config_file(const std::string& config_path);

/**
 * @brief Start the runtime (blocking operation)
 * 
 * This method blocks the calling thread and begins processing tasks.
 * It returns only when stop() is called from another thread or
 * when a fatal error occurs.
 * 
 * @throws RuntimeException if runtime is not initialized or start fails
 */
static void start();

/**
 * @brief Stop the runtime gracefully
 * @param timeout Maximum time to wait for graceful shutdown
 * 
 * Initiates graceful shutdown of all worker threads and backends.
 * Allows currently executing tasks to complete within the timeout period.
 */
static void stop(std::chrono::milliseconds timeout = std::chrono::seconds(30));

/**
 * @brief Restart the runtime with new configuration
 * @param new_config New runtime configuration
 * @param graceful_timeout Timeout for graceful shutdown of old runtime
 * 
 * Performs a graceful restart by stopping the current runtime and
 * starting a new one with the updated configuration.
 */
static void restart(const RuntimeConfig& new_config, 
                   std::chrono::milliseconds graceful_timeout = std::chrono::seconds(30));

/**
 * @brief Check if runtime is currently running
 * @return true if runtime is active and processing tasks
 */
static bool is_running() noexcept;

/**
 * @brief Check if runtime has been initialized
 * @return true if init() has been called successfully
 */
static bool is_initialized() noexcept;

// ========================================================================
// Task Scheduling
// ========================================================================

/**
 * @brief Schedule a task for execution
 * @param task Task to be scheduled
 * 
 * Adds the task to the scheduling queue. The task will be executed
 * according to the configured scheduling strategy.
 */
static void schedule(Task<> task);

/**
 * @brief Schedule a task with specific priority
 * @param task Task to be scheduled
 * @param priority Task priority (higher values = higher priority)
 * 
 * Only effective when using PRIORITY scheduling strategy.
 * Priority range: 0 (lowest) to 100 (highest).
 */
static void schedule_with_priority(Task<> task, int priority);

/**
 * @brief Schedule a task on a specific worker thread
 * @param task Task to be scheduled
 * @param worker_id Target worker thread ID
 * 
 * Forces the task to be executed on the specified worker.
 * Useful for CPU affinity or data locality optimizations.
 */
static void schedule_on_worker(Task<> task, int worker_id);

/**
 * @brief Schedule a task with custom scheduling hint
 * @param task Task to be scheduled
 * @param hint Custom scheduling hint (implementation-specific)
 */
static void schedule_with_hint(Task<> task, const SchedulingHint& hint);

/**
 * @brief Get the number of pending tasks in the system
 * @return Total number of tasks waiting to be executed
 */
static size_t pending_task_count() noexcept;

/**
 * @brief Get the number of currently executing tasks
 * @return Number of tasks currently being processed
 */
static size_t active_task_count() noexcept;

// ========================================================================
// Performance Monitoring and Statistics
// ========================================================================

/**
 * @brief Get comprehensive runtime statistics
 * @return Current runtime performance statistics
 * 
 * Provides detailed information about task execution, worker performance,
 * memory usage, and backend-specific metrics.
 */
static RuntimeStats get_stats();

/**
 * @brief Get per-worker performance statistics
 * @return Vector of worker-specific statistics
 */
static std::vector<WorkerStats> get_worker_stats();

/**
 * @brief Enable or disable performance profiling
 * @param enabled Whether to enable detailed profiling
 * 
 * When enabled, collects detailed performance metrics at the cost
 * of some performance overhead. Useful for debugging and optimization.
 */
static void enable_profiling(bool enabled);

/**
 * @brief Reset all performance statistics
 * 
 * Clears all accumulated performance counters and resets timing measurements.
 * Useful for benchmarking specific time periods.
 */
static void reset_stats();

/**
 * @brief Add a custom performance metric
 * @param name Metric name
 * @param value Metric value
 */
static void add_metric(const std::string& name, double value);

/**
 * @brief Increment a counter metric
 * @param name Counter name
 * @param delta Amount to increment (default: 1)
 */
static void increment_metric(const std::string& name, size_t delta = 1);

/**
 * @brief Set a gauge metric value
 * @param name Gauge name  
 * @param value Current gauge value
 */
static void set_gauge(const std::string& name, double value);

// ========================================================================
// Configuration Management
// ========================================================================

/**
 * @brief Get current runtime configuration
 * @return Copy of the current configuration
 */
static RuntimeConfig get_config();

/**
 * @brief Update runtime configuration (hot reload)
 * @param new_config New configuration to apply
 * @throws RuntimeException if configuration is invalid or cannot be applied
 * 
 * Updates configuration without restarting the runtime.
 * Only a subset of configuration options support hot reloading.
 */
static void update_config(const RuntimeConfig& new_config);

/**
 * @brief Validate a configuration without applying it
 * @param config Configuration to validate
 * @return Validation result with detailed error information
 */
static ValidationResult validate_config(const RuntimeConfig& config);

// ========================================================================
// Request Recovery System
// ========================================================================

/**
 * @brief Enable request recovery system
 * 
 * Activates the fault-tolerant request processing system.
 * Must be called before registering recovery hooks.
 */
static void enable_request_recovery();

/**
 * @brief Disable request recovery system
 * 
 * Deactivates request recovery and cleans up recovery resources.
 */
static void disable_request_recovery();

/**
 * @brief Register a recovery hook for failed requests
 * @param hook Recovery function to call for failed requests
 * 
 * The hook will be called when a request fails and needs recovery.
 * Multiple hooks can be registered and will be called in registration order.
 */
static void on_recover(RecoveryHook hook);

/**
 * @brief Register an advanced recovery hook with metadata
 * @param hook Advanced recovery function with failure metadata
 */
static void on_recover_advanced(AdvancedRecoveryHook hook);

/**
 * @brief Register a task error handler
 * @param handler Function to call when task execution fails
 * 
 * Called for unhandled exceptions in task execution.
 * Useful for centralized error logging and reporting.
 */
static void on_task_error(TaskErrorHandler handler);

/**
 * @brief Manually trigger recovery for a specific request
 * @param ctx Request context
 * @param req Request to recover
 * @return Recovery task
 */
static Task<> recover_request(Context& ctx, const Request& req);

// ========================================================================
// Backend Management
// ========================================================================

/**
 * @brief Get information about the current backend
 * @return Backend information structure
 */
static BackendInfo get_backend_info();

/**
 * @brief Get list of available backends on current platform
 * @return Vector of supported backend types
 */
static std::vector<RuntimeBackend> get_available_backends();

/**
 * @brief Register a custom backend factory
 * @param name Backend name
 * @param factory Backend creation function
 */
static void register_backend(const std::string& name, 
                            std::function<std::unique_ptr<IEventLoopBackend>()> factory);

/**
 * @brief Register a custom scheduler factory
 * @param name Scheduler name
 * @param factory Scheduler creation function
 */
static void register_scheduler(const std::string& name,
                              std::function<std::unique_ptr<ITaskScheduler>()> factory);

// ========================================================================
// Debugging and Diagnostics
// ========================================================================

/**
 * @brief Generate a comprehensive runtime diagnostic report
 * @return Detailed diagnostic information
 * 
 * Includes configuration, performance metrics, worker status,
 * and backend-specific diagnostic information.
 */
static std::string diagnostic_report();

/**
 * @brief Dump current task queue state for debugging
 * @return Task queue diagnostic information
 */
static std::string dump_task_queues();

/**
 * @brief Get list of currently executing tasks
 * @return Vector of active task information
 */
static std::vector<TaskInfo> get_active_tasks();

/**
 * @brief Enable debug mode with detailed logging
 * @param enabled Whether to enable debug mode
 */
static void enable_debug_mode(bool enabled);

/**
 * @brief Trigger a controlled shutdown for testing
 * @param exit_code Exit code to use
 * 
 * Used for testing graceful shutdown behavior.
 * Should not be used in production code.
 */
static void debug_shutdown(int exit_code = 0);

// ========================================================================
// Integration Points
// ========================================================================

/**
 * @brief Set the default context factory for new requests
 * @param factory Function to create new contexts
 */
static void set_context_factory(std::function<std::unique_ptr<Context>(std::string)> factory);

/**
 * @brief Set the logger instance for runtime operations
 * @param logger Logger to use for runtime messages
 */
static void set_logger(std::shared_ptr<Logger> logger);

/**
 * @brief Get the current runtime logger
 * @return Shared pointer to runtime logger
 */
static std::shared_ptr<Logger> get_logger();

// ========================================================================
// Advanced Features
// ========================================================================

/**
 * @brief Execute a function in the runtime context
 * @param func Function to execute
 * @return Future for the execution result
 * 
 * Executes the function within the runtime's event loop context.
 * Useful for thread-safe operations from external threads.
 */
template<typename F>
static std::future<std::invoke_result_t<F>> execute_in_runtime(F&& func);

/**
 * @brief Schedule a periodic task
 * @param task Task to execute periodically
 * @param interval Execution interval
 * @return Handle to cancel the periodic task
 */
static PeriodicTaskHandle schedule_periodic(Task<> task, std::chrono::milliseconds interval);

/**
 * @brief Schedule a delayed task
 * @param task Task to execute after delay
 * @param delay Delay before execution
 * @return Handle to cancel the delayed task
 */
static DelayedTaskHandle schedule_delayed(Task<> task, std::chrono::milliseconds delay);

/**
 * @brief Create a task completion source
 * @return Task completion source for manual task completion
 */
template<typename T = void>
static TaskCompletionSource<T> create_completion_source();
```

private:
// ========================================================================
// Internal Implementation (Not part of public API)
// ========================================================================

```
Runtime() = delete;
~Runtime() = delete;
Runtime(const Runtime&) = delete;
Runtime& operator=(const Runtime&) = delete;

// Internal state management
static void initialize_backend(const RuntimeConfig& config);
static void initialize_scheduler(const RuntimeConfig& config);
static void initialize_workers(const RuntimeConfig& config);
static void initialize_recovery_system(const RuntimeConfig& config);
static void cleanup_resources();

// Task execution
static void execute_task_internal(Task<> task);
static void handle_task_exception(const std::exception& e, const Task<>& task);

// Statistics collection
static void update_statistics();
static void collect_worker_statistics();

// Recovery operations
static void journal_request_start(const Context& ctx, const Request& req);
static void journal_request_complete(const std::string& trace_id);
static void process_recovery_queue();
```

};

// ============================================================================
// Helper Types and Structures
// ============================================================================

/**

- @brief Information about an active task
  */
  struct TaskInfo {
  std::string name;                           ///< Task name (if set)
  std::string trace_id;                       ///< Associated trace ID
  TaskState state;                            ///< Current task state
  std::chrono::steady_clock::time_point created_at; ///< Creation timestamp
  std::chrono::steady_clock::time_point started_at; ///< Execution start time
  int worker_id;                              ///< Executing worker ID (-1 if not started)
  size_t priority;                            ///< Task priority
  };

/**

- @brief Backend information structure
  */
  struct BackendInfo {
  RuntimeBackend type;                        ///< Backend type
  std::string name;                           ///< Backend name
  std::string version;                        ///< Backend version
  bool supports_threading;                   ///< Multi-threading support
  bool supports_io_uring;                    ///< io_uring support
  size_t max_connections;                     ///< Maximum supported connections
  std::vector<std::string> features;         ///< Supported features
  };

/**

- @brief Scheduling hint for advanced task placement
  */
  struct SchedulingHint {
  std::optional<int> preferred_worker;        ///< Preferred worker ID
  std::optional<int> numa_node;               ///< Preferred NUMA node
  bool cpu_intensive = false;                 ///< Whether task is CPU-intensive
  bool io_intensive = false;                  ///< Whether task is I/O-intensive
  std::string affinity_group;                ///< Affinity group name
  };

/**

- @brief Handle for periodic task management
  */
  class PeriodicTaskHandle {
  public:
  void cancel();
  bool is_cancelled() const;
  std::chrono::milliseconds interval() const;
  size_t execution_count() const;

private:
friend class Runtime;
explicit PeriodicTaskHandle(size_t id) : id_(id) {}
size_t id_;
};

/**

- @brief Handle for delayed task management
  */
  class DelayedTaskHandle {
  public:
  void cancel();
  bool is_cancelled() const;
  std::chrono::milliseconds remaining_delay() const;

private:
friend class Runtime;
explicit DelayedTaskHandle(size_t id) : id_(id) {}
size_t id_;
};

/**

- @brief Task completion source for manual task completion
  */
  template<typename T = void>
  class TaskCompletionSource {
  public:
  Task<T> get_task();
  void set_result(T result);
  void set_exception(std::exception_ptr exception);
  bool is_completed() const;

private:
friend class Runtime;
TaskCompletionSource() = default;
// Implementation details…
};

// ============================================================================
// Template Method Implementations
// ============================================================================

template<typename F>
std::future<std::invoke_result_t<F>> Runtime::execute_in_runtime(F&& func) {
using ReturnType = std::invoke_result_t<F>;
auto promise = std::make_shared<std::promise<ReturnType>>();
auto future = promise->get_future();

```
schedule([promise, func = std::forward<F>(func)]() -> Task<> {
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
    co_return;
}());

return future;
```

}

template<typename T>
TaskCompletionSource<T> Runtime::create_completion_source() {
return TaskCompletionSource<T>{};
}

} // namespace stellane
