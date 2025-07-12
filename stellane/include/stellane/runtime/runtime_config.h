// include/stellane/runtime/runtime_config.h
#pragma once

#include <string>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <optional>
#include <filesystem>

namespace stellane {

// ============================================================================
// Task Scheduling Strategies
// ============================================================================

/**

- @brief Task scheduling strategies for the runtime
  */
  enum class TaskSchedulingStrategy {
  FIFO,           ///< First-in, first-out (default)
  PRIORITY,       ///< Priority-based scheduling
  AFFINITY,       ///< CPU/worker affinity
  WORK_STEALING,  ///< Dynamic load balancing
  CUSTOM          ///< User-defined scheduler
  };

/**

- @brief Convert scheduling strategy to string
  */
  std::string to_string(TaskSchedulingStrategy strategy);

/**

- @brief Convert string to scheduling strategy
  */
  std::optional<TaskSchedulingStrategy> from_string(const std::string& str);

// ============================================================================
// Recovery Configuration
// ============================================================================

/**

- @brief Request recovery configuration
  */
  struct RecoveryConfig {
  bool enabled = false;                                   ///< Enable recovery system
  std::string backend = “mmap”;                          ///< Recovery backend (mmap, rocksdb, leveldb)
  std::filesystem::path storage_path = “/tmp/stellane”; ///< Recovery data storage path
  int max_attempts = 3;                                  ///< Maximum recovery attempts per request
  std::chrono::milliseconds retry_delay{100};           ///< Base delay between retry attempts
  bool exponential_backoff = true;                      ///< Use exponential backoff for retries
  size_t max_journal_size_mb = 100;                     ///< Maximum journal file size in MB
  std::chrono::hours journal_retention{24};             ///< How long to keep recovery data
  
  // Validation
  bool validate() const {
  return max_attempts > 0 &&
  retry_delay.count() >= 0 &&
  max_journal_size_mb > 0 &&
  !backend.empty() &&
  !storage_path.empty();
  }
  };

// ============================================================================
// Performance Tuning Configuration
// ============================================================================

/**

- @brief Performance tuning options
  */
  struct PerformanceConfig {
  // Memory management
  bool enable_memory_pooling = true;                    ///< Use memory pools for frequent allocations
  size_t context_pool_size = 1000;                     ///< Pre-allocated Context objects
  size_t task_pool_size = 10000;                       ///< Pre-allocated Task objects
  bool enable_zero_copy_io = true;                     ///< Use zero-copy I/O when available
  
  // CPU optimization
  bool enable_cpu_affinity = false;                    ///< Pin threads to specific CPU cores
  std::vector<int> cpu_cores;                          ///< Specific CPU cores to use (empty = auto)
  bool enable_numa_awareness = false;                  ///< NUMA-aware memory allocation
  
  // I/O optimization
  int io_batch_size = 32;                              ///< Batch size for I/O operations
  std::chrono::microseconds io_timeout{1000};         ///< I/O operation timeout
  bool enable_io_uring = true;                         ///< Use io_uring on Linux (if available)
  
  // Task scheduling
  int max_tasks_per_iteration = 1000;                  ///< Max tasks to process per event loop iteration
  std::chrono::microseconds yield_threshold{100};     ///< Yield CPU if no work for this long
  bool enable_task_stealing = false;                   ///< Enable work stealing between threads
  
  // Validation
  bool validate() const {
  return context_pool_size > 0 &&
  task_pool_size > 0 &&
  io_batch_size > 0 &&
  io_timeout.count() >= 0 &&
  max_tasks_per_iteration > 0 &&
  yield_threshold.count() >= 0;
  }
  };

// ============================================================================
// Monitoring and Observability Configuration
// ============================================================================

/**

- @brief Monitoring and observability configuration
  */
  struct MonitoringConfig {
  // Metrics collection
  bool enable_metrics = true;                           ///< Enable metrics collection
  std::chrono::milliseconds metrics_interval{1000};   ///< Metrics collection interval
  bool enable_detailed_metrics = false;                ///< Collect detailed per-request metrics
  
  // Logging
  std::string log_level = “info”;                      ///< Log level (trace, debug, info, warn, error)
  bool enable_structured_logging = true;               ///< Use structured (JSON) logging
  bool enable_async_logging = true;                    ///< Use asynchronous logging
  size_t log_buffer_size = 10000;                     ///< Log buffer size for async logging
  
  // Tracing
  bool enable_distributed_tracing = false;             ///< Enable distributed tracing
  std::string tracing_endpoint;                        ///< Tracing collector endpoint
  double tracing_sample_rate = 0.1;                   ///< Fraction of requests to trace (0.0-1.0)
  
  // Health checks
  bool enable_health_checks = true;                    ///< Enable health check endpoints
  std::chrono::seconds health_check_interval{30};     ///< Health check frequency
  
  // Validation
  bool validate() const {
  return metrics_interval.count() > 0 &&
  log_buffer_size > 0 &&
  tracing_sample_rate >= 0.0 && tracing_sample_rate <= 1.0 &&
  health_check_interval.count() > 0;
  }
  };

// ============================================================================
// Main Runtime Configuration
// ============================================================================

/**

- @brief Complete runtime configuration
  */
  struct RuntimeConfig {
  // ========================================================================
  // Core Runtime Settings
  // ========================================================================
  
  std::string backend_type = “libuv”;                  ///< Event loop backend (libuv, epoll, io_uring)
  int worker_threads = 1;                              ///< Number of worker threads (1 = single-threaded)
  TaskSchedulingStrategy scheduling_strategy = TaskSchedulingStrategy::FIFO;
  
  // Connection and request limits
  size_t max_connections = 10000;                      ///< Maximum concurrent connections
  size_t max_requests_per_connection = 1000;           ///< Max requests per connection (HTTP/1.1)
  std::chrono::seconds connection_timeout{30};         ///< Connection timeout
  std::chrono::seconds request_timeout{30};            ///< Request processing timeout
  std::chrono::seconds keep_alive_timeout{5};          ///< Keep-alive timeout
  
  // Buffer sizes
  size_t read_buffer_size = 8192;                      ///< Read buffer size per connection
  size_t write_buffer_size = 8192;                     ///< Write buffer size per connection
  size_t max_request_body_size = 1024 * 1024;          ///< Maximum request body size (1MB)
  size_t max_header_size = 8192;                       ///< Maximum header size
  
  // ========================================================================
  // Component Configurations
  // ========================================================================
  
  RecoveryConfig recovery;                              ///< Request recovery configuration
  PerformanceConfig performance;                        ///< Performance tuning options
  MonitoringConfig monitoring;                          ///< Monitoring and observability
  
  // ========================================================================
  // Custom Configuration
  // ========================================================================
  
  std::unordered_map<std::string, std::string> custom_options; ///< Backend-specific or custom options
  
  // ========================================================================
  // Configuration Management
  // ========================================================================
  
  /**
  - @brief Validate the entire configuration
  - @return true if valid, false otherwise
    */
    bool validate() const;
  
  /**
  - @brief Get configuration as key-value pairs (for debugging)
  - @return Map of all configuration values
    */
    std::unordered_map<std::string, std::string> to_map() const;
  
  /**
  - @brief Load configuration from file
  - @param file_path Path to configuration file (TOML format)
  - @return Loaded configuration
  - @throws std::runtime_error if file cannot be loaded or parsed
    */
    static RuntimeConfig from_file(const std::filesystem::path& file_path);
  
  /**
  - @brief Load configuration from environment variables
  - @return Configuration with values from environment
  - 
  - Environment variable format: STELLANE_<SECTION>_<KEY>
  - Examples:
  - - STELLANE_BACKEND_TYPE=libuv
  - - STELLANE_WORKER_THREADS=4
  - - STELLANE_RECOVERY_ENABLED=true
      */
      static RuntimeConfig from_environment();
  
  /**
  - @brief Save configuration to file
  - @param file_path Path where to save the configuration
  - @param format File format (“toml”, “json”, “yaml”)
  - @throws std::runtime_error if file cannot be written
    */
    void save_to_file(const std::filesystem::path& file_path,
    const std::string& format = “toml”) const;
  
  /**
  - @brief Merge with another configuration (other takes precedence)
  - @param other Configuration to merge
  - @return Merged configuration
    */
    RuntimeConfig merge(const RuntimeConfig& other) const;
  
  /**
  - @brief Create a configuration optimized for specific use cases
    */
    static RuntimeConfig for_development();               ///< Development-friendly settings
    static RuntimeConfig for_production();                ///< Production-optimized settings
    static RuntimeConfig for_high_frequency_trading();    ///< Ultra-low latency settings
    static RuntimeConfig for_gaming();                    ///< Game server optimized settings
    static RuntimeConfig for_web_services();              ///< Web API optimized settings
  
  // ========================================================================
  // Backward Compatibility
  // ========================================================================
  
  /**
  - @brief Create configuration from the simple format used in docs
  - @param backend_type Backend type string
  - @param worker_threads Number of worker threads
  - @return Basic configuration
    */
    static RuntimeConfig create_basic(const std::string& backend_type = “libuv”,
    int worker_threads = 1);
    };

// ============================================================================
// Configuration Validation Helpers
// ============================================================================

/**

- @brief Configuration validation result
  */
  struct ValidationResult {
  bool is_valid = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
  
  void add_error(const std::string& error) {
  is_valid = false;
  errors.push_back(error);
  }
  
  void add_warning(const std::string& warning) {
  warnings.push_back(warning);
  }
  
  std::string to_string() const;
  };

/**

- @brief Detailed configuration validator
  */
  class ConfigValidator {
  public:
  static ValidationResult validate(const RuntimeConfig& config);
  static ValidationResult validate_for_platform();  ///< Check platform-specific requirements
  static ValidationResult validate_performance_settings(const PerformanceConfig& perf);
  static ValidationResult validate_recovery_settings(const RecoveryConfig& recovery);
  static ValidationResult validate_monitoring_settings(const MonitoringConfig& monitoring);
  };

// ============================================================================
// Configuration Presets
// ============================================================================

/**

- @brief Pre-defined configuration presets for common scenarios
  */
  namespace config_presets {

/**

- @brief Minimal configuration for testing
  */
  inline RuntimeConfig minimal() {
  RuntimeConfig config;
  config.worker_threads = 1;
  config.max_connections = 100;
  config.monitoring.enable_metrics = false;
  config.recovery.enabled = false;
  return config;
  }

/**

- @brief High-performance configuration for real-time systems
  */
  inline RuntimeConfig high_performance() {
  RuntimeConfig config;
  config.backend_type = “io_uring”;  // Fallback to epoll, then libuv
  config.worker_threads = std::thread::hardware_concurrency();
  config.scheduling_strategy = TaskSchedulingStrategy::WORK_STEALING;
  config.performance.enable_cpu_affinity = true;
  config.performance.enable_numa_awareness = true;
  config.performance.enable_zero_copy_io = true;
  config.performance.io_batch_size = 64;
  config.max_connections = 100000;
  return config;
  }

/**

- @brief Fault-tolerant configuration with recovery enabled
  */
  inline RuntimeConfig fault_tolerant() {
  RuntimeConfig config;
  config.recovery.enabled = true;
  config.recovery.backend = “rocksdb”;
  config.recovery.max_attempts = 5;
  config.recovery.exponential_backoff = true;
  config.monitoring.enable_health_checks = true;
  config.monitoring.enable_distributed_tracing = true;
  return config;
  }

} // namespace config_presets

} // namespace stellane
