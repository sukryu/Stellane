#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <functional>
#include <memory>
#include <thread>
#include <filesystem>

namespace stellane {

// Forward declarations
class IEventLoopBackend;
class ITaskScheduler;

// ============================================================================
// Runtime Backend Types
// ============================================================================

/**

- @brief Available event loop backend implementations
- 
- Each backend offers different performance characteristics and platform support:
- - LIBUV: Cross-platform, balanced performance
- - EPOLL: Linux-only, high performance single-threaded
- - IO_URING: Linux 5.1+, ultra-high performance
- - STELLANE: Custom multi-threaded implementation
    */
    enum class RuntimeBackend : int {
    LIBUV = 0,        ///< Cross-platform libuv backend
    EPOLL = 1,        ///< Linux epoll single-threaded backend  
    IO_URING = 2,     ///< Linux io_uring ultra-high performance
    STELLANE = 3,     ///< Custom Stellane multi-threaded backend
    CUSTOM = 999      ///< User-defined custom backend
    };

/**

- @brief Task scheduling strategies for different workload patterns
  */
  enum class TaskSchedulingStrategy : int {
  FIFO = 0,           ///< First-in, first-out (simple, predictable)
  PRIORITY = 1,       ///< Priority-based scheduling
  WORK_STEALING = 2,  ///< Work-stealing for multi-core optimization
  AFFINITY = 3,       ///< CPU/worker affinity-based
  ROUND_ROBIN = 4,    ///< Round-robin distribution
  CUSTOM = 999        ///< User-defined scheduling strategy
  };

/**

- @brief Recovery journal backend implementations
  */
  enum class RecoveryBackend : int {
  DISABLED = 0,     ///< No recovery journaling
  MMAP = 1,         ///< Memory-mapped file journal
  ROCKSDB = 2,      ///< RocksDB persistent storage
  LEVELDB = 3,      ///< LevelDB persistent storage
  CUSTOM = 999      ///< User-defined recovery backend
  };

// ============================================================================
// Configuration Structures
// ============================================================================

/**

- @brief Performance tuning configuration
  */
  struct PerformanceConfig {
  bool enable_profiling = false;              ///< Enable runtime performance profiling
  bool zero_copy_io = true;                   ///< Enable zero-copy I/O optimizations
  bool numa_aware = false;                    ///< Enable NUMA-aware memory allocation
  size_t io_batch_size = 32;                  ///< I/O operation batch size
  std::chrono::milliseconds idle_timeout{100}; ///< Worker idle timeout
  
  /**
  - @brief Task pool configuration
    */
    struct TaskPool {
    size_t initial_size = 1000;            ///< Initial task pool size
    size_t max_size = 10000;               ///< Maximum task pool size
    bool enable_pooling = true;            ///< Enable task object pooling
    } task_pool;
  
  /**
  - @brief Memory allocation settings
    */
    struct Memory {
    size_t context_pool_size = 5000;       ///< Context object pool size
    bool use_custom_allocator = false;     ///< Use custom memory allocator
    size_t stack_size_kb = 64;             ///< Coroutine stack size (KB)
    } memory;
    };

/**

- @brief Recovery system configuration
  */
  struct RecoveryConfig {
  bool enabled = false;                       ///< Enable request recovery system
  RecoveryBackend backend = RecoveryBackend::MMAP; ///< Recovery storage backend
  std::string journal_path = “/tmp/stellane/recovery”; ///< Journal storage path
  size_t max_recovery_attempts = 3;          ///< Maximum recovery retry attempts
  std::chrono::seconds recovery_timeout{30}; ///< Recovery operation timeout
  
  /**
  - @brief Journal rotation settings
    */
    struct Journal {
    size_t max_file_size_mb = 100;         ///< Maximum journal file size
    size_t max_files = 10;                 ///< Maximum number of journal files
    bool compress_old_files = true;        ///< Compress rotated journal files
    } journal;
  
  /**
  - @brief Recovery filters
    */
    struct Filters {
    std::vector<std::string> excluded_paths; ///< Paths to exclude from recovery
    std::vector<std::string> excluded_methods; ///< HTTP methods to exclude
    bool recover_only_idempotent = true;   ///< Only recover idempotent operations
    } filters;
    };

/**

- @brief Worker thread configuration
  */
  struct WorkerConfig {
  size_t worker_threads = std::thread::hardware_concurrency(); ///< Number of worker threads
  size_t max_tasks_per_loop = 1000;          ///< Maximum tasks per event loop iteration
  bool enable_cpu_affinity = false;          ///< Bind workers to specific CPU cores
  
  /**
  - @brief CPU affinity settings
    */
    struct Affinity {
    std::vector<size_t> cpu_cores;         ///< Specific CPU cores to use (empty = auto)
    bool isolate_main_thread = false;      ///< Isolate main thread to separate core
    bool respect_numa_topology = true;     ///< Respect NUMA topology
    } affinity;
  
  /**
  - @brief Work-stealing configuration
    */
    struct WorkStealing {
    bool enabled = true;                    ///< Enable work-stealing between workers
    size_t steal_threshold = 10;           ///< Minimum tasks before stealing
    std::chrono::microseconds steal_interval{100}; ///< Work-stealing check interval
    } work_stealing;
    };

/**

- @brief Main runtime configuration structure
- 
- This structure contains all configuration options for the Stellane runtime system.
- It supports both programmatic configuration and TOML file-based configuration.
- 
- @example
- ```cpp
  
  ```
- RuntimeConfig config;
- config.backend = RuntimeBackend::IO_URING;
- config.worker.worker_threads = 8;
- config.performance.enable_profiling = true;
- Runtime::init(config);
- ```
  
  ```

*/
struct RuntimeConfig {
// ========================================================================
// Core Runtime Settings
// ========================================================================

```
RuntimeBackend backend = RuntimeBackend::LIBUV; ///< Event loop backend
TaskSchedulingStrategy strategy = TaskSchedulingStrategy::FIFO; ///< Task scheduling strategy

/**
 * @brief Custom backend factory function
 * Used when backend == RuntimeBackend::CUSTOM
 */
std::function<std::unique_ptr<IEventLoopBackend>()> custom_backend_factory;

/**
 * @brief Custom scheduler factory function  
 * Used when strategy == TaskSchedulingStrategy::CUSTOM
 */
std::function<std::unique_ptr<ITaskScheduler>()> custom_scheduler_factory;

// ========================================================================
// Sub-configurations
// ========================================================================

WorkerConfig worker;                        ///< Worker thread configuration
PerformanceConfig performance;              ///< Performance tuning settings
RecoveryConfig recovery;                    ///< Recovery system settings

// ========================================================================
// Logging Integration
// ========================================================================

/**
 * @brief Runtime logging configuration
 */
struct Logging {
    std::string component = "stellane.runtime"; ///< Log component name
    bool enable_trace_logging = false;         ///< Enable detailed trace logs
    bool log_task_lifecycle = false;           ///< Log task creation/completion
    bool log_worker_stats = true;              ///< Log worker statistics
} logging;

// ========================================================================
// Advanced Settings
// ========================================================================

/**
 * @brief Experimental features (use with caution)
 */
struct Experimental {
    bool enable_task_priorities = false;       ///< Enable task priority system
    bool enable_adaptive_scaling = false;      ///< Enable adaptive worker scaling
    bool enable_jit_compilation = false;       ///< Enable JIT compilation for hot paths
} experimental;

// ========================================================================
// Factory Methods
// ========================================================================

/**
 * @brief Create default production configuration
 * @return Optimized configuration for production use
 */
static RuntimeConfig production();

/**
 * @brief Create development configuration with debugging enabled
 * @return Configuration optimized for development and debugging
 */
static RuntimeConfig development();

/**
 * @brief Create high-performance configuration for real-time applications
 * @return Configuration optimized for latency-sensitive applications
 */
static RuntimeConfig high_performance();

/**
 * @brief Load configuration from TOML file
 * @param config_path Path to TOML configuration file
 * @return Parsed configuration or std::nullopt on error
 */
static std::optional<RuntimeConfig> from_toml_file(const std::filesystem::path& config_path);

/**
 * @brief Load configuration from TOML string
 * @param toml_content TOML configuration content
 * @return Parsed configuration or std::nullopt on error
 */
static std::optional<RuntimeConfig> from_toml_string(const std::string& toml_content);

/**
 * @brief Load configuration from environment variables
 * @return Configuration with environment variable overrides applied
 * 
 * Environment variables:
 * - STELLANE_RUNTIME_BACKEND
 * - STELLANE_RUNTIME_WORKERS
 * - STELLANE_RUNTIME_STRATEGY
 * - etc.
 */
static RuntimeConfig from_environment();

// ========================================================================
// Validation and Serialization
// ========================================================================

/**
 * @brief Validate configuration consistency and platform compatibility
 * @return Validation result with error details
 */
[[nodiscard]] ValidationResult validate() const;

/**
 * @brief Convert configuration to TOML format
 * @param pretty_print Enable pretty-printed output
 * @return TOML configuration string
 */
[[nodiscard]] std::string to_toml(bool pretty_print = true) const;

/**
 * @brief Apply environment variable overrides
 * @return Modified configuration with environment overrides
 */
RuntimeConfig with_environment_overrides() const;

/**
 * @brief Merge with another configuration (other takes precedence)
 * @param other Configuration to merge
 * @return Merged configuration
 */
RuntimeConfig merge(const RuntimeConfig& other) const;

// ========================================================================
// Runtime Information
// ========================================================================

/**
 * @brief Get estimated memory usage for this configuration
 * @return Estimated memory usage in bytes
 */
[[nodiscard]] size_t estimated_memory_usage() const;

/**
 * @brief Check if configuration is suitable for current platform
 * @return Platform compatibility information
 */
[[nodiscard]] PlatformCompatibility check_platform_compatibility() const;

/**
 * @brief Get human-readable configuration summary
 * @return Configuration summary string
 */
[[nodiscard]] std::string summary() const;
```

};

// ============================================================================
// Validation and Compatibility Types
// ============================================================================

/**

- @brief Configuration validation result
  */
  struct ValidationResult {
  bool is_valid = true;                       ///< Overall validation status
  std::vector<std::string> errors;           ///< Critical errors (prevent startup)
  std::vector<std::string> warnings;         ///< Non-critical warnings
  std::vector<std::string> suggestions;      ///< Performance optimization suggestions
  
  /**
  - @brief Check if configuration has any issues
  - @return true if there are errors or warnings
    */
    [[nodiscard]] bool has_issues() const noexcept {
    return !errors.empty() || !warnings.empty();
    }
  
  /**
  - @brief Get formatted validation report
  - @return Human-readable validation report
    */
    [[nodiscard]] std::string report() const;
    };

/**

- @brief Platform compatibility information
  */
  struct PlatformCompatibility {
  bool is_supported = true;                   ///< Overall platform support
  std::string platform_name;                 ///< Detected platform name
  std::string recommended_backend;            ///< Recommended backend for this platform
  
  struct Features {
  bool io_uring_available = false;       ///< io_uring support available
  bool epoll_available = false;          ///< epoll support available
  bool numa_available = false;           ///< NUMA support available
  size_t max_threads = 0;                ///< Maximum recommended threads
  } features;
  
  std::vector<std::string> limitations;      ///< Platform-specific limitations
  std::vector<std::string> recommendations;  ///< Platform-specific recommendations
  };

// ============================================================================
// Utility Functions
// ============================================================================

/**

- @brief Convert backend enum to string representation
- @param backend Runtime backend enum
- @return String representation
  */
  [[nodiscard]] std::string to_string(RuntimeBackend backend);

/**

- @brief Parse backend from string representation
- @param backend_str String representation
- @return Backend enum or std::nullopt if invalid
  */
  [[nodiscard]] std::optional<RuntimeBackend> backend_from_string(const std::string& backend_str);

/**

- @brief Convert scheduling strategy enum to string representation
- @param strategy Scheduling strategy enum
- @return String representation
  */
  [[nodiscard]] std::string to_string(TaskSchedulingStrategy strategy);

/**

- @brief Parse scheduling strategy from string representation
- @param strategy_str String representation
- @return Strategy enum or std::nullopt if invalid
  */
  [[nodiscard]] std::optional<TaskSchedulingStrategy> strategy_from_string(const std::string& strategy_str);

/**

- @brief Get optimal configuration for current hardware
- @return Hardware-optimized configuration
  */
  [[nodiscard]] RuntimeConfig auto_detect_optimal_config();

/**

- @brief Get available backends on current platform
- @return List of supported backends
  */
  [[nodiscard]] std::vector<RuntimeBackend> get_available_backends();

/**

- @brief Check if specific backend is available on current platform
- @param backend Backend to check
- @return true if backend is available
  */
  [[nodiscard]] bool is_backend_available(RuntimeBackend backend);

} // namespace stellane
