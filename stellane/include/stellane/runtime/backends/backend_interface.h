// include/stellane/runtime/backends/backend_interface.h
#pragma once

#include “stellane/runtime/event_loop.h”
#include “stellane/core/task.h”
#include <string>
#include <chrono>
#include <memory>
#include <functional>
#include <vector>
#include <atomic>
#include <mutex>

namespace stellane {

// ============================================================================
// Backend Performance Metrics
// ============================================================================

/**

- @brief Performance metrics for event loop backends
  */
  struct BackendMetrics {
  std::chrono::steady_clock::time_point start_time;
  std::chrono::milliseconds uptime;
  
  // Task statistics
  size_t total_tasks_scheduled = 0;
  size_t total_tasks_executed = 0;
  size_t total_tasks_failed = 0;
  size_t current_pending_tasks = 0;
  size_t peak_pending_tasks = 0;
  
  // Performance metrics
  double average_task_execution_time_us = 0.0;
  double average_queue_wait_time_us = 0.0;
  size_t events_processed_per_second = 0;
  
  // Memory usage (approximate)
  size_t estimated_memory_usage_bytes = 0;
  
  // Backend-specific metrics
  std::unordered_map<std::string, double> custom_metrics;
  
  // Helper methods
  double get_task_failure_rate() const {
  return total_tasks_executed > 0 ?
  static_cast<double>(total_tasks_failed) / total_tasks_executed : 0.0;
  }
  
  double get_throughput_tasks_per_second() const {
  auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
  return uptime_seconds > 0 ?
  static_cast<double>(total_tasks_executed) / uptime_seconds : 0.0;
  }
  };

// ============================================================================
// Backend Configuration
// ============================================================================

/**

- @brief Configuration options for backend implementations
  */
  struct BackendConfig {
  // General settings
  int max_events_per_iteration = 1000;
  std::chrono::microseconds max_wait_time{1000};
  bool enable_metrics_collection = true;
  
  // Performance tuning
  bool enable_batch_processing = true;
  int batch_size = 32;
  bool enable_cpu_affinity = false;
  int cpu_core_id = -1;  // -1 means no affinity
  
  // Memory management
  size_t initial_task_queue_capacity = 1000;
  size_t max_task_queue_size = 100000;
  bool enable_memory_pooling = true;
  
  // Error handling
  int max_consecutive_errors = 100;
  std::chrono::milliseconds error_cooldown_period{1000};
  
  // Backend-specific configuration
  std::unordered_map<std::string, std::string> backend_options;
  
  // Validation
  bool validate() const {
  return max_events_per_iteration > 0 &&
  max_wait_time.count() >= 0 &&
  batch_size > 0 &&
  initial_task_queue_capacity > 0 &&
  max_task_queue_size >= initial_task_queue_capacity &&
  max_consecutive_errors > 0;
  }
  };

// ============================================================================
// Backend Capability Flags
// ============================================================================

/**

- @brief Capability flags for backend implementations
  */
  enum class BackendCapability : uint32_t {
  NONE = 0,
  
  // Basic capabilities
  SINGLE_THREADED = 1 << 0,
  MULTI_THREADED = 1 << 1,
  
  // I/O capabilities
  FILE_IO = 1 << 2,
  NETWORK_IO = 1 << 3,
  TIMER_SUPPORT = 1 << 4,
  SIGNAL_HANDLING = 1 << 5,
  
  // Performance features
  ZERO_COPY_IO = 1 << 6,
  BATCH_OPERATIONS = 1 << 7,
  CPU_AFFINITY = 1 << 8,
  NUMA_AWARENESS = 1 << 9,
  
  // Platform-specific
  WINDOWS_IOCP = 1 << 10,
  LINUX_EPOLL = 1 << 11,
  LINUX_IO_URING = 1 << 12,
  BSD_KQUEUE = 1 << 13,
  
  // Cross-platform
  CROSS_PLATFORM = 1 << 14,
  
  // Advanced features
  LOAD_BALANCING = 1 << 15,
  AUTO_SCALING = 1 << 16,
  FAULT_TOLERANCE = 1 << 17
  };

// Bitwise operations for capability flags
inline BackendCapability operator|(BackendCapability a, BackendCapability b) {
return static_cast<BackendCapability>(
static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
);
}

inline BackendCapability operator&(BackendCapability a, BackendCapability b) {
return static_cast<BackendCapability>(
static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
);
}

inline bool has_capability(BackendCapability flags, BackendCapability capability) {
return (flags & capability) == capability;
}

// ============================================================================
// Enhanced Backend Interface
// ============================================================================

/**

- @brief Enhanced backend interface with additional capabilities
- 
- This interface extends IEventLoopBackend with more detailed control,
- metrics collection, and configuration options.
  */
  class IEnhancedEventLoopBackend : public IEventLoopBackend {
  public:
  virtual ~IEnhancedEventLoopBackend() = default;
  
  // ========================================================================
  // Configuration and Initialization
  // ========================================================================
  
  /**
  - @brief Initialize the backend with specific configuration
  - @param config Backend configuration
  - @return true on success, false on failure
    */
    virtual bool initialize(const BackendConfig& config) = 0;
  
  /**
  - @brief Get current backend configuration
  - @return Current configuration
    */
    virtual BackendConfig get_config() const = 0;
  
  /**
  - @brief Update backend configuration (if supported)
  - @param config New configuration
  - @return true if update successful, false otherwise
    */
    virtual bool update_config(const BackendConfig& config) = 0;
  
  // ========================================================================
  // Capability Information
  // ========================================================================
  
  /**
  - @brief Get backend capabilities
  - @return Capability flags
    */
    virtual BackendCapability get_capabilities() const = 0;
  
  /**
  - @brief Check if backend supports specific capability
  - @param capability Capability to check
  - @return true if supported, false otherwise
    */
    virtual bool supports(BackendCapability capability) const = 0;
  
  /**
  - @brief Get backend version information
  - @return Version string
    */
    virtual std::string get_version() const = 0;
  
  /**
  - @brief Get platform-specific information
  - @return Platform info (OS, architecture, etc.)
    */
    virtual std::string get_platform_info() const = 0;
  
  // ========================================================================
  // Advanced Task Scheduling
  // ========================================================================
  
  /**
  - @brief Schedule a task with priority
  - @param task Task to execute
  - @param priority Priority level (higher = more important)
    */
    virtual void schedule_with_priority(Task<> task, int priority = 0) = 0;
  
  /**
  - @brief Schedule a delayed task
  - @param task Task to execute
  - @param delay Delay before execution
    */
    virtual void schedule_delayed(Task<> task, std::chrono::milliseconds delay) = 0;
  
  /**
  - @brief Schedule a recurring task
  - @param task Task to execute repeatedly
  - @param interval Time between executions
  - @return Task ID for cancellation
    */
    virtual uint64_t schedule_recurring(Task<> task, std::chrono::milliseconds interval) = 0;
  
  /**
  - @brief Cancel a recurring task
  - @param task_id Task ID returned by schedule_recurring
  - @return true if cancelled, false if not found
    */
    virtual bool cancel_recurring_task(uint64_t task_id) = 0;
  
  // ========================================================================
  // Performance Monitoring
  // ========================================================================
  
  /**
  - @brief Get current performance metrics
  - @return Current metrics
    */
    virtual BackendMetrics get_metrics() const = 0;
  
  /**
  - @brief Reset performance metrics
    */
    virtual void reset_metrics() = 0;
  
  /**
  - @brief Enable or disable metrics collection
  - @param enabled true to enable, false to disable
    */
    virtual void set_metrics_enabled(bool enabled) = 0;
  
  // ========================================================================
  // Health and Diagnostics
  // ========================================================================
  
  /**
  - @brief Perform health check
  - @return true if healthy, false if issues detected
    */
    virtual bool health_check() const = 0;
  
  /**
  - @brief Get diagnostic information
  - @return Diagnostic data as key-value pairs
    */
    virtual std::unordered_map<std::string, std::string> get_diagnostics() const = 0;
  
  /**
  - @brief Get current load factor (0.0 = idle, 1.0 = fully loaded)
  - @return Load factor
    */
    virtual double get_load_factor() const = 0;
  
  // ========================================================================
  // Event Callbacks (Optional)
  // ========================================================================
  
  using ErrorCallback = std::function<void(const std::string& error, int error_code)>;
  using MetricsCallback = std::function<void(const BackendMetrics& metrics)>;
  
  /**
  - @brief Set error callback for async error reporting
  - @param callback Callback function
    */
    virtual void set_error_callback(ErrorCallback callback) = 0;
  
  /**
  - @brief Set metrics callback for periodic metrics reporting
  - @param callback Callback function
  - @param interval Reporting interval
    */
    virtual void set_metrics_callback(MetricsCallback callback,
    std::chrono::milliseconds interval) = 0;
    };

// ============================================================================
// Backend Base Implementation
// ============================================================================

/**

- @brief Base implementation providing common functionality
- 
- Concrete backends can inherit from this class to get default
- implementations of common features.
  */
  class BaseEventLoopBackend : public IEnhancedEventLoopBackend {
  public:
  BaseEventLoopBackend();
  virtual ~BaseEventLoopBackend() = default;
  
  // Basic interface (still pure virtual)
  int run() override = 0;
  void stop(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) override = 0;
  void schedule(Task<> task) override = 0;
  bool is_running() const override = 0;
  size_t pending_tasks() const override = 0;
  std::string name() const override = 0;
  
  // Enhanced interface (default implementations)
  bool initialize(const BackendConfig& config) override;
  BackendConfig get_config() const override;
  bool update_config(const BackendConfig& config) override;
  
  BackendCapability get_capabilities() const override;
  bool supports(BackendCapability capability) const override;
  std::string get_version() const override;
  std::string get_platform_info() const override;
  
  void schedule_with_priority(Task<> task, int priority = 0) override;
  void schedule_delayed(Task<> task, std::chrono::milliseconds delay) override;
  uint64_t schedule_recurring(Task<> task, std::chrono::milliseconds interval) override;
  bool cancel_recurring_task(uint64_t task_id) override;
  
  BackendMetrics get_metrics() const override;
  void reset_metrics() override;
  void set_metrics_enabled(bool enabled) override;
  
  bool health_check() const override;
  std::unordered_map<std::string, std::string> get_diagnostics() const override;
  double get_load_factor() const override;
  
  void set_error_callback(ErrorCallback callback) override;
  void set_metrics_callback(MetricsCallback callback,
  std::chrono::milliseconds interval) override;

protected:
// Configuration
BackendConfig config_;
mutable std::mutex config_mutex_;

```
// Metrics
mutable BackendMetrics metrics_;
mutable std::mutex metrics_mutex_;
std::atomic<bool> metrics_enabled_{true};

// Callbacks
ErrorCallback error_callback_;
MetricsCallback metrics_callback_;
std::chrono::milliseconds metrics_interval_{1000};

// State
std::atomic<bool> initialized_{false};
std::atomic<uint64_t> next_task_id_{1};

// Helper methods
void update_metrics_timestamp();
void record_task_scheduled();
void record_task_executed(std::chrono::microseconds execution_time);
void record_task_failed();
void report_error(const std::string& error, int error_code = 0);

// Pure virtual methods for derived classes
virtual BackendCapability get_native_capabilities() const = 0;
virtual std::string get_backend_version() const = 0;
```

};

} // namespace stellane
