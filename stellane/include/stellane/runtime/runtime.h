// include/stellane/runtime/runtime.h
#pragma once

#include “stellane/runtime/runtime_config.h”
#include “stellane/runtime/event_loop.h”
#include “stellane/runtime/backends/backend_interface.h”
#include “stellane/core/task.h”
#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <chrono>

namespace stellane {

// ============================================================================
// Runtime Statistics and Monitoring
// ============================================================================

/**

- @brief Comprehensive runtime statistics
  */
  struct RuntimeStats {
  std::chrono::steady_clock::time_point start_time;
  std::chrono::milliseconds uptime;
  
  // Task statistics
  size_t total_tasks_scheduled = 0;
  size_t total_tasks_executed = 0;
  size_t total_tasks_failed = 0;
  size_t current_active_tasks = 0;
  size_t peak_active_tasks = 0;
  
  // Performance metrics
  double average_task_duration_ms = 0.0;
  double tasks_per_second = 0.0;
  
  // Backend information
  std::string backend_type;
  int worker_count = 0;
  std::vector<BackendMetrics> worker_metrics;
  
  // System resources
  size_t memory_usage_bytes = 0;
  double cpu_usage_percent = 0.0;
  
  // Health status
  bool is_healthy = true;
  std::vector<std::string> health_issues;
  
  // Helper methods
  double get_task_failure_rate() const {
  return total_tasks_executed > 0 ?
  static_cast<double>(total_tasks_failed) / total_tasks_executed : 0.0;
  }
  
  std::chrono::milliseconds get_average_task_duration() const {
  return std::chrono::milliseconds(static_cast<long>(average_task_duration_ms));
  }
  };

// ============================================================================
// Runtime Event Callbacks
// ============================================================================

/**

- @brief Runtime event callbacks for monitoring and debugging
  */
  struct RuntimeCallbacks {
  using ErrorCallback = std::function<void(const std::string& error, int error_code)>;
  using TaskErrorCallback = std::function<void(const std::exception& e, const std::string& task_name)>;
  using MetricsCallback = std::function<void(const RuntimeStats& stats)>;
  using ShutdownCallback = std::function<void()>;
  using HealthCheckCallback = std::function<bool()>;
  
  ErrorCallback on_error;                        ///< Called on runtime errors
  TaskErrorCallback on_task_error;               ///< Called when task execution fails
  MetricsCallback on_metrics_update;             ///< Called periodically with stats
  ShutdownCallback on_shutdown;                  ///< Called during shutdown
  HealthCheckCallback on_health_check;           ///< Called during health checks
  };

// ============================================================================
// Backend Factory Registry
// ============================================================================

/**

- @brief Registry for event loop backend factories
  */
  class BackendRegistry {
  public:
  using BackendFactory = std::function<std::unique_ptr<IEventLoopBackend>()>;
  using EnhancedBackendFactory = std::function<std::unique_ptr<IEnhancedEventLoopBackend>()>;
  
  /**
  - @brief Register a basic backend factory
  - @param name Backend name (e.g., “libuv”, “epoll”)
  - @param factory Factory function
    */
    static void register_backend(const std::string& name, BackendFactory factory);
  
  /**
  - @brief Register an enhanced backend factory
  - @param name Backend name
  - @param factory Enhanced factory function
    */
    static void register_enhanced_backend(const std::string& name, EnhancedBackendFactory factory);
  
  /**
  - @brief Create a backend instance
  - @param name Backend name
  - @return Backend instance or nullptr if not found
    */
    static std::unique_ptr<IEventLoopBackend> create_backend(const std::string& name);
  
  /**
  - @brief Create an enhanced backend instance
  - @param name Backend name
  - @return Enhanced backend instance or nullptr if not found
    */
    static std::unique_ptr<IEnhancedEventLoopBackend> create_enhanced_backend(const std::string& name);
  
  /**
  - @brief List all registered backends
  - @return Vector of backend names
    */
    static std::vector<std::string> list_backends();
  
  /**
  - @brief Check if backend is registered
  - @param name Backend name
  - @return true if registered, false otherwise
    */
    static bool has_backend(const std::string& name);
  
  /**
  - @brief Get backend capabilities without creating instance
  - @param name Backend name
  - @return Capability flags or NONE if not found
    */
    static BackendCapability get_backend_capabilities(const std::string& name);

private:
static std::unordered_map<std::string, BackendFactory> factories_;
static std::unordered_map<std::string, EnhancedBackendFactory> enhanced_factories_;
static std::unordered_map<std::string, BackendCapability> capabilities_;
static std::mutex registry_mutex_;
};

// ============================================================================
// Main Runtime Class
// ============================================================================

/**

- @brief Main runtime management class
- 
- The Runtime class is the central coordinator for Stellane’s asynchronous
- execution environment. It manages event loop backends, task scheduling,
- and provides monitoring capabilities.
  */
  class Runtime {
  public:
  // ========================================================================
  // Lifecycle Management
  // ========================================================================
  
  /**
  - @brief Initialize the runtime with configuration
  - @param config Runtime configuration
  - @throws std::runtime_error if initialization fails
    */
    static void initialize(const RuntimeConfig& config = RuntimeConfig{});
  
  /**
  - @brief Initialize from configuration file
  - @param config_path Path to configuration file
  - @throws std::runtime_error if file cannot be loaded or runtime init fails
    */
    static void initialize_from_file(const std::filesystem::path& config_path);
  
  /**
  - @brief Initialize from environment variables
  - @throws std::runtime_error if initialization fails
    */
    static void initialize_from_environment();
  
  /**
  - @brief Start the runtime (blocking call)
  - @return Exit code (0 = success, non-zero = error)
  - 
  - This method blocks until stop() is called or an unrecoverable error occurs.
  - It’s typically called from main() after setting up routes and middleware.
    */
    static int start();
  
  /**
  - @brief Start the runtime asynchronously
  - @return Future that completes when runtime stops
    */
    static std::future<int> start_async();
  
  /**
  - @brief Stop the runtime gracefully
  - @param timeout Maximum time to wait for graceful shutdown
    */
    static void stop(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
  
  /**
  - @brief Shutdown and cleanup the runtime
  - 
  - This should be called when the application is terminating.
  - After calling this, initialize() must be called again before using the runtime.
    */
    static void shutdown();
  
  /**
  - @brief Check if runtime is initialized
  - @return true if initialized, false otherwise
    */
    static bool is_initialized();
  
  /**
  - @brief Check if runtime is currently running
  - @return true if running, false otherwise
    */
    static bool is_running();
  
  // ========================================================================
  // Task Scheduling Interface
  // ========================================================================
  
  /**
  - @brief Schedule a task for immediate execution
  - @param task Task to execute
  - @throws std::runtime_error if runtime not initialized
    */
    static void schedule(Task<> task);
  
  /**
  - @brief Schedule a task with priority
  - @param task Task to execute
  - @param priority Priority level (higher = more important)
    */
    static void schedule_with_priority(Task<> task, int priority);
  
  /**
  - @brief Schedule a delayed task
  - @param task Task to execute
  - @param delay Delay before execution
  - @return Task ID for cancellation (if supported)
    */
    static uint64_t schedule_delayed(Task<> task, std::chrono::milliseconds delay);
  
  /**
  - @brief Schedule a recurring task
  - @param task Task to execute repeatedly
  - @param interval Time between executions
  - @return Task ID for cancellation
    */
    static uint64_t schedule_recurring(Task<> task, std::chrono::milliseconds interval);
  
  /**
  - @brief Cancel a scheduled task
  - @param task_id Task ID returned by schedule_delayed or schedule_recurring
  - @return true if cancelled, false if not found or already executed
    */
    static bool cancel_task(uint64_t task_id);
  
  // ========================================================================
  // Configuration and Monitoring
  // ========================================================================
  
  /**
  - @brief Get current runtime configuration
  - @return Current configuration
    */
    static RuntimeConfig get_config();
  
  /**
  - @brief Update runtime configuration (limited support)
  - @param config New configuration
  - @return true if update successful, false if changes require restart
  - 
  - Note: Some configuration changes require runtime restart
    */
    static bool update_config(const RuntimeConfig& config);
  
  /**
  - @brief Get current runtime statistics
  - @return Current statistics
    */
    static RuntimeStats get_stats();
  
  /**
  - @brief Reset runtime statistics
    */
    static void reset_stats();
  
  /**
  - @brief Perform runtime health check
  - @return true if healthy, false if issues detected
    */
    static bool health_check();
  
  /**
  - @brief Get detailed diagnostic information
  - @return Diagnostic data as key-value pairs
    */
    static std::unordered_map<std::string, std::string> get_diagnostics();
  
  // ========================================================================
  // Event Callbacks
  // ========================================================================
  
  /**
  - @brief Set runtime event callbacks
  - @param callbacks Callback functions for various events
    */
    static void set_callbacks(const RuntimeCallbacks& callbacks);
  
  /**
  - @brief Set error callback
  - @param callback Function to call on runtime errors
    */
    static void set_error_callback(RuntimeCallbacks::ErrorCallback callback);
  
  /**
  - @brief Set task error callback
  - @param callback Function to call when task execution fails
    */
    static void set_task_error_callback(RuntimeCallbacks::TaskErrorCallback callback);
  
  /**
  - @brief Set metrics callback
  - @param callback Function to call with periodic metrics
  - @param interval How often to call the callback
    */
    static void set_metrics_callback(RuntimeCallbacks::MetricsCallback callback,
    std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
  
  // ========================================================================
  // Backend Management
  // ========================================================================
  
  /**
  - @brief Get current backend name
  - @return Backend name (e.g., “libuv”, “epoll”)
    */
    static std::string get_backend_name();
  
  /**
  - @brief Get current backend capabilities
  - @return Capability flags
    */
    static BackendCapability get_backend_capabilities();
  
  /**
  - @brief List all available backends
  - @return Vector of backend names
    */
    static std::vector<std::string> list_available_backends();
  
  /**
  - @brief Check if specific backend is available
  - @param backend_name Backend name to check
  - @return true if available, false otherwise
    */
    static bool is_backend_available(const std::string& backend_name);
  
  // ========================================================================
  // Advanced Features
  // ========================================================================
  
  /**
  - @brief Enable request recovery system
  - @param recovery_config Recovery configuration
  - @throws std::runtime_error if recovery cannot be enabled
    */
    static void enable_request_recovery(const RecoveryConfig& recovery_config = RecoveryConfig{});
  
  /**
  - @brief Set request recovery handler
  - @param handler Function to handle request recovery
    */
    static void set_recovery_handler(std::function<Task<>(Context&, const Request&)> handler);
  
  /**
  - @brief Force garbage collection (if applicable)
  - 
  - Triggers cleanup of unused resources, completed tasks, etc.
  - This is usually called automatically but can be triggered manually.
    */
    static void garbage_collect();
  
  /**
  - @brief Optimize runtime for current workload
  - 
  - Analyzes current performance metrics and adjusts internal parameters
  - for better performance. This is experimental and may not always help.
    */
    static void optimize_for_workload();
  
  // ========================================================================
  // Testing and Development Support
  // ========================================================================
  
  /**
  - @brief Create a test runtime with minimal configuration
  - @param backend_name Backend to use for testing
  - @return Test configuration
  - 
  - This is useful for unit tests and development environments.
    */
    static RuntimeConfig create_test_config(const std::string& backend_name = “libuv”);
  
  /**
  - @brief Run a single task synchronously (for testing)
  - @param task Task to execute
  - @param timeout Maximum time to wait
  - @return true if task completed successfully
  - 
  - This bypasses the normal event loop and runs the task immediately.
  - Only use for testing!
    */
    static bool run_task_sync(Task<> task, std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

private:
// ========================================================================
// Internal State
// ========================================================================

```
Runtime() = delete;  // Static-only class

// Core state
static std::unique_ptr<IEnhancedEventLoopBackend> backend_;
static RuntimeConfig config_;
static RuntimeStats stats_;
static RuntimeCallbacks callbacks_;

// Synchronization
static std::mutex state_mutex_;
static std::atomic<bool> initialized_;
static std::atomic<bool> running_;
static std::atomic<bool> shutdown_requested_;

// Background threads
static std::unique_ptr<std::thread> metrics_thread_;
static std::unique_ptr<std::thread> health_check_thread_;

// Recovery system
static std::function<Task<>(Context&, const Request&)> recovery_handler_;

// ========================================================================
// Internal Helper Methods
// ========================================================================

static void validate_config(const RuntimeConfig& config);
static void create_backend();
static void start_background_threads();
static void stop_background_threads();
static void metrics_worker();
static void health_check_worker();
static void update_stats();
static void handle_error(const std::string& error, int error_code = 0);
static void handle_task_error(const std::exception& e, const std::string& task_name);
static void cleanup_resources();
```

};

// ============================================================================
// Auto-registration Helper Macros
// ============================================================================

/**

- @brief Register a backend automatically at program startup
- @param name Backend name
- @param class_type Backend class type
  */
  #define STELLANE_REGISTER_BACKEND(name, class_type)   
  namespace {   
  struct class_type##Registrar {   
  class_type##Registrar() {   
  BackendRegistry::register_backend(name, []() -> std::unique_ptr<IEventLoopBackend> {   
  return std::make_unique<class_type>();   
  });   
  }   
  };   
  static class_type##Registrar g_##class_type##_registrar;   
  }

/**

- @brief Register an enhanced backend automatically at program startup
- @param name Backend name
- @param class_type Enhanced backend class type
  */
  #define STELLANE_REGISTER_ENHANCED_BACKEND(name, class_type)   
  namespace {   
  struct class_type##EnhancedRegistrar {   
  class_type##EnhancedRegistrar() {   
  BackendRegistry::register_enhanced_backend(name, []() -> std::unique_ptr<IEnhancedEventLoopBackend> {   
  return std::make_unique<class_type>();   
  });   
  }   
  };   
  static class_type##EnhancedRegistrar g_##class_type##_enhanced_registrar;   
  }

// ============================================================================
// Convenience Functions
// ============================================================================

/**

- @brief Initialize and start runtime in one call
- @param config Runtime configuration
- @return Exit code
  */
  inline int run_stellane_runtime(const RuntimeConfig& config = RuntimeConfig{}) {
  Runtime::initialize(config);
  return Runtime::start();
  }

/**

- @brief Initialize and start runtime from config file
- @param config_path Path to configuration file
- @return Exit code
  */
  inline int run_stellane_runtime(const std::filesystem::path& config_path) {
  Runtime::initialize_from_file(config_path);
  return Runtime::start();
  }

} // namespace stellane
