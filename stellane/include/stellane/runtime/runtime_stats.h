#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <atomic>
#include <unordered_map>
#include <thread>
#include <optional>
#include <memory>

namespace stellane::runtime {

// ============================================================================
// Forward Declarations
// ============================================================================

class IEventLoopBackend;
class ITaskScheduler;

// ============================================================================
// Core Statistics Types
// ============================================================================

/**

- @brief High-level runtime performance statistics
- 
- Aggregated metrics for the entire runtime system including all workers,
- backends, and scheduling components. Updated atomically for thread safety.
- 
- **Real-time Focus**: Optimized for sub-millisecond latency monitoring
- suitable for chat/metaverse applications requiring < 200ms response times.
  */
  struct RuntimeStats {
  // ========================================================================
  // Temporal Metrics
  // ========================================================================
  
  std::chrono::steady_clock::time_point start_time;    ///< Runtime initialization timestamp
  std::chrono::milliseconds uptime{0};                 ///< Total runtime uptime
  std::chrono::steady_clock::time_point last_update;   ///< Last statistics update
  
  // ========================================================================
  // Task Execution Metrics
  // ========================================================================
  
  std::atomic<size_t> total_tasks_executed{0};         ///< Total tasks completed since start
  std::atomic<size_t> total_tasks_failed{0};           ///< Total tasks that failed execution
  std::atomic<size_t> current_active_tasks{0};         ///< Currently executing tasks
  std::atomic<size_t> current_pending_tasks{0};        ///< Tasks waiting for execution
  std::atomic<size_t> peak_active_tasks{0};            ///< Maximum concurrent active tasks
  std::atomic<size_t> peak_pending_tasks{0};           ///< Maximum pending tasks reached
  
  // ========================================================================
  // Performance Metrics
  // ========================================================================
  
  std::atomic<double> average_task_duration_ms{0.0};   ///< Moving average task execution time
  std::atomic<double> p95_task_duration_ms{0.0};       ///< 95th percentile task duration
  std::atomic<double> p99_task_duration_ms{0.0};       ///< 99th percentile task duration
  std::atomic<size_t> tasks_per_second{0};             ///< Current tasks/second throughput
  std::atomic<size_t> peak_tasks_per_second{0};        ///< Peak throughput recorded
  
  // ========================================================================
  // Memory Usage Metrics
  // ========================================================================
  
  std::atomic<size_t> heap_usage_bytes{0};             ///< Current heap memory usage
  std::atomic<size_t> peak_heap_usage_bytes{0};        ///< Peak heap usage reached
  std::atomic<size_t> task_pool_usage_bytes{0};        ///< Memory used by task pools
  std::atomic<size_t> stack_usage_bytes{0};            ///< Combined stack usage of all workers
  
  // ========================================================================
  // Configuration Info
  // ========================================================================
  
  std::string backend_type;                            ///< Active backend type (“epoll”, “io_uring”, etc.)
  int worker_count{0};                                 ///< Number of worker threads
  std::string scheduling_strategy;                     ///< Active scheduling strategy
  bool profiling_enabled{false};                       ///< Whether detailed profiling is active
  
  // ========================================================================
  // Recovery System Metrics
  // ========================================================================
  
  std::atomic<size_t> total_recovery_attempts{0};      ///< Total recovery operations attempted
  std::atomic<size_t> successful_recoveries{0};        ///< Successfully recovered requests
  std::atomic<size_t> failed_recoveries{0};            ///< Failed recovery attempts
  std::atomic<double> average_recovery_time_ms{0.0};   ///< Average recovery operation time
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Calculate success rate percentage
  - @return Task success rate (0.0 - 100.0)
    */
    [[nodiscard]] double success_rate() const noexcept {
    auto total = total_tasks_executed.load() + total_tasks_failed.load();
    return total > 0 ? (static_cast<double>(total_tasks_executed.load()) * 100.0 / total) : 100.0;
    }
  
  /**
  - @brief Calculate recovery success rate
  - @return Recovery success rate (0.0 - 100.0)
    */
    [[nodiscard]] double recovery_success_rate() const noexcept {
    auto total = total_recovery_attempts.load();
    return total > 0 ? (static_cast<double>(successful_recoveries.load()) * 100.0 / total) : 100.0;
    }
  
  /**
  - @brief Calculate current load factor
  - @return Load factor (0.0 = idle, 1.0 = fully loaded)
    */
    [[nodiscard]] double load_factor() const noexcept {
    return worker_count > 0 ?
    std::min(1.0, static_cast<double>(current_active_tasks.load()) / (worker_count * 10.0)) : 0.0;
    }
  
  /**
  - @brief Check if system is under high load
  - @param threshold Load threshold (default: 0.8)
  - @return true if system load exceeds threshold
    */
    [[nodiscard]] bool is_high_load(double threshold = 0.8) const noexcept {
    return load_factor() > threshold;
    }
  
  /**
  - @brief Generate formatted statistics report
  - @return Human-readable statistics summary
    */
    [[nodiscard]] std::string to_string() const;
    };

/**

- @brief Per-worker thread performance statistics
- 
- Detailed metrics for individual worker threads, essential for identifying
- load balancing issues and optimizing work-stealing algorithms.
  */
  struct WorkerStats {
  // ========================================================================
  // Worker Identity
  // ========================================================================
  
  int worker_id{-1};                                   ///< Worker thread identifier
  std::thread::id thread_id;                           ///< System thread ID
  int cpu_core{-1};                                    ///< Bound CPU core (-1 if not bound)
  int numa_node{-1};                                   ///< NUMA node (-1 if unknown)
  
  // ========================================================================
  // Task Execution Metrics
  // ========================================================================
  
  std::atomic<size_t> tasks_executed{0};              ///< Total tasks executed by this worker
  std::atomic<size_t> tasks_failed{0};                ///< Tasks that failed on this worker
  std::atomic<size_t> current_queue_size{0};          ///< Current task queue depth
  std::atomic<size_t> peak_queue_size{0};             ///< Maximum queue depth reached
  std::atomic<size_t> tasks_stolen_from{0};           ///< Tasks stolen by other workers
  std::atomic<size_t> tasks_stolen_to{0};             ///< Tasks stolen from other workers
  
  // ========================================================================
  // Performance Metrics
  // ========================================================================
  
  std::atomic<double> cpu_usage_percent{0.0};         ///< CPU usage percentage (0-100)
  std::atomic<double> average_task_duration_ms{0.0};  ///< Average task execution time
  std::atomic<size_t> tasks_per_second{0};            ///< Current worker throughput
  std::chrono::steady_clock::time_point last_activity; ///< Last task execution timestamp
  
  // ========================================================================
  // Work-Stealing Metrics
  // ========================================================================
  
  std::atomic<size_t> steal_attempts{0};              ///< Work stealing attempts made
  std::atomic<size_t> successful_steals{0};           ///< Successful work steals
  std::atomic<size_t> steal_rejections{0};            ///< Work stealing rejections
  std::atomic<double> steal_efficiency{0.0};          ///< Steal success rate (0-100)
  
  // ========================================================================
  // Memory Metrics
  // ========================================================================
  
  std::atomic<size_t> stack_usage_bytes{0};           ///< Worker stack memory usage
  std::atomic<size_t> local_heap_bytes{0};            ///< Worker-local heap allocations
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Check if worker is considered idle
  - @param idle_threshold Idle time threshold
  - @return true if worker has been idle longer than threshold
    */
    [[nodiscard]] bool is_idle(std::chrono::milliseconds idle_threshold) const noexcept {
    return std::chrono::steady_clock::now() - last_activity > idle_threshold;
    }
  
  /**
  - @brief Calculate worker load factor
  - @return Load factor (0.0 = idle, 1.0 = fully loaded)
    */
    [[nodiscard]] double load_factor() const noexcept {
    return std::min(1.0, current_queue_size.load() / 100.0); // Assuming max 100 optimal
    }
  
  /**
  - @brief Calculate work stealing efficiency
  - @return Efficiency percentage (0-100)
    */
    [[nodiscard]] double work_stealing_efficiency() const noexcept {
    auto attempts = steal_attempts.load();
    return attempts > 0 ?
    (static_cast<double>(successful_steals.load()) * 100.0 / attempts) : 100.0;
    }
  
  /**
  - @brief Get worker utilization score
  - @return Utilization score combining CPU usage and queue depth
    */
    [[nodiscard]] double utilization_score() const noexcept {
    return (cpu_usage_percent.load() + load_factor() * 100.0) / 2.0;
    }
    };

/**

- @brief Backend-specific performance statistics
- 
- Metrics specific to the event loop backend implementation,
- allowing optimization of backend-specific parameters.
  */
  struct BackendStats {
  // ========================================================================
  // I/O Event Metrics
  // ========================================================================
  
  std::atomic<size_t> total_io_events{0};             ///< Total I/O events processed
  std::atomic<size_t> read_events{0};                 ///< Read events handled
  std::atomic<size_t> write_events{0};                ///< Write events handled
  std::atomic<size_t> error_events{0};                ///< Error events encountered
  std::atomic<size_t> timeout_events{0};              ///< Timeout events processed
  
  // ========================================================================
  // Backend-Specific Metrics
  // ========================================================================
  
  std::atomic<double> average_poll_time_ms{0.0};      ///< Average polling operation time
  std::atomic<size_t> poll_calls{0};                  ///< Total polling system calls
  std::atomic<size_t> poll_timeouts{0};               ///< Polling timeouts occurred
  std::atomic<size_t> events_per_poll{0};             ///< Average events per poll call
  
  // ========================================================================
  // io_uring Specific (when applicable)
  // ========================================================================
  
  std::atomic<size_t> submission_queue_depth{0};      ///< Current SQ depth
  std::atomic<size_t> completion_queue_depth{0};      ///< Current CQ depth
  std::atomic<size_t> total_submissions{0};           ///< Total SQE submissions
  std::atomic<size_t> total_completions{0};           ///< Total CQE completions
  std::atomic<size_t> batch_submissions{0};           ///< Batched submissions count
  std::atomic<double> batch_efficiency{0.0};          ///< Batching efficiency (0-100)
  
  // ========================================================================
  // Connection Metrics
  // ========================================================================
  
  std::atomic<size_t> active_connections{0};          ///< Currently active connections
  std::atomic<size_t> peak_connections{0};            ///< Peak concurrent connections
  std::atomic<size_t> total_connections_accepted{0};  ///< Total connections accepted
  std::atomic<size_t> connections_closed{0};          ///< Total connections closed
  std::atomic<double> connection_duration_ms{0.0};    ///< Average connection lifetime
  
  // ========================================================================
  // Performance Metrics
  // ========================================================================
  
  std::atomic<size_t> bytes_read{0};                  ///< Total bytes read
  std::atomic<size_t> bytes_written{0};               ///< Total bytes written
  std::atomic<double> read_throughput_mbps{0.0};      ///< Read throughput (MB/s)
  std::atomic<double> write_throughput_mbps{0.0};     ///< Write throughput (MB/s)
  
  /**
  - @brief Calculate I/O efficiency
  - @return I/O efficiency score (0-100)
    */
    [[nodiscard]] double io_efficiency() const noexcept {
    auto total_events = total_io_events.load();
    auto error_rate = total_events > 0 ?
    (static_cast<double>(error_events.load()) / total_events) : 0.0;
    return std::max(0.0, (1.0 - error_rate) * 100.0);
    }
  
  /**
  - @brief Get combined throughput
  - @return Total throughput in MB/s
    */
    [[nodiscard]] double total_throughput_mbps() const noexcept {
    return read_throughput_mbps.load() + write_throughput_mbps.load();
    }
    };

// ============================================================================
// Advanced Profiling Support
// ============================================================================

/**

- @brief Detailed profiling information (collected when profiling is enabled)
- 
- High-resolution timing and diagnostic information for performance optimization.
- **Warning**: Enabling profiling adds ~5-10% performance overhead.
  */
  struct ProfilingStats {
  // ========================================================================
  // Task Lifecycle Timing
  // ========================================================================
  
  std::vector<double> task_creation_times_ms;         ///< Task creation latencies
  std::vector<double> task_scheduling_times_ms;       ///< Scheduling overhead times
  std::vector<double> task_execution_times_ms;        ///< Actual execution times
  std::vector<double> task_completion_times_ms;       ///< Completion processing times
  
  // ========================================================================
  // Memory Allocation Tracking
  // ========================================================================
  
  std::atomic<size_t> total_allocations{0};           ///< Total memory allocations
  std::atomic<size_t> total_deallocations{0};         ///< Total memory deallocations
  std::atomic<size_t> peak_allocation_size{0};        ///< Largest single allocation
  std::atomic<double> average_allocation_size{0.0};   ///< Average allocation size
  
  // ========================================================================
  // System Resource Usage
  // ========================================================================
  
  std::atomic<double> system_cpu_usage{0.0};          ///< System-wide CPU usage
  std::atomic<size_t> system_memory_usage{0};         ///< System memory usage
  std::atomic<size_t> file_descriptors_used{0};       ///< Open file descriptors
  std::atomic<size_t> context_switches{0};            ///< Context switch count
  
  /**
  - @brief Calculate memory fragmentation score
  - @return Fragmentation score (0-100, lower is better)
    */
    [[nodiscard]] double memory_fragmentation() const noexcept {
    // Simplified fragmentation calculation
    auto allocs = total_allocations.load();
    auto deallocs = total_deallocations.load();
    return allocs > 0 ?
    (static_cast<double>(allocs - deallocs) * 100.0 / allocs) : 0.0;
    }
    };

// ============================================================================
// Statistics Collection Interface
// ============================================================================

/**

- @brief Interface for statistics collection and aggregation
- 
- Defines the contract for collecting, updating, and retrieving
- runtime performance statistics. Implementations must be thread-safe.
  */
  class IStatsCollector {
  public:
  virtual ~IStatsCollector() = default;
  
  // ========================================================================
  // Statistics Retrieval
  // ========================================================================
  
  /**
  - @brief Get comprehensive runtime statistics
  - @return Current runtime statistics snapshot
    */
    virtual RuntimeStats get_runtime_stats() const = 0;
  
  /**
  - @brief Get per-worker statistics
  - @return Vector of worker statistics
    */
    virtual std::vector<WorkerStats> get_worker_stats() const = 0;
  
  /**
  - @brief Get backend-specific statistics
  - @return Backend performance statistics
    */
    virtual BackendStats get_backend_stats() const = 0;
  
  /**
  - @brief Get profiling statistics (if enabled)
  - @return Detailed profiling information
    */
    virtual std::optional<ProfilingStats> get_profiling_stats() const = 0;
  
  // ========================================================================
  // Statistics Management
  // ========================================================================
  
  /**
  - @brief Reset all statistics counters
    */
    virtual void reset_stats() = 0;
  
  /**
  - @brief Enable or disable profiling
  - @param enabled Whether to enable detailed profiling
    */
    virtual void enable_profiling(bool enabled) = 0;
  
  /**
  - @brief Update statistics (called periodically by runtime)
    */
    virtual void update_stats() = 0;
  
  // ========================================================================
  // Event Recording
  // ========================================================================
  
  /**
  - @brief Record task execution event
  - @param worker_id Worker that executed the task
  - @param duration_ms Task execution time
  - @param success Whether task completed successfully
    */
    virtual void record_task_execution(int worker_id, double duration_ms, bool success) = 0;
  
  /**
  - @brief Record I/O event
  - @param event_type Type of I/O event
  - @param bytes_processed Number of bytes processed
    */
    virtual void record_io_event(const std::string& event_type, size_t bytes_processed) = 0;
  
  /**
  - @brief Record memory allocation
  - @param size_bytes Size of allocation
    */
    virtual void record_allocation(size_t size_bytes) = 0;
  
  /**
  - @brief Record memory deallocation
  - @param size_bytes Size of deallocation
    */
    virtual void record_deallocation(size_t size_bytes) = 0;
    };

// ============================================================================
// Statistics Export and Formatting
// ============================================================================

/**

- @brief Statistics formatting and export utilities
  */
  namespace stats_formatter {
  
  /**
  - @brief Format runtime statistics as JSON
  - @param stats Runtime statistics to format
  - @return JSON representation
    */
    std::string to_json(const RuntimeStats& stats);
  
  /**
  - @brief Format worker statistics as JSON
  - @param worker_stats Vector of worker statistics
  - @return JSON representation
    */
    std::string to_json(const std::vector<WorkerStats>& worker_stats);
  
  /**
  - @brief Format backend statistics as JSON
  - @param backend_stats Backend statistics
  - @return JSON representation
    */
    std::string to_json(const BackendStats& backend_stats);
  
  /**
  - @brief Generate comprehensive statistics report
  - @param runtime_stats Runtime statistics
  - @param worker_stats Worker statistics
  - @param backend_stats Backend statistics
  - @return Human-readable report
    */
    std::string generate_report(const RuntimeStats& runtime_stats,
    const std::vector<WorkerStats>& worker_stats,
    const BackendStats& backend_stats);
  
  /**
  - @brief Export statistics to CSV format
  - @param filename Output filename
  - @param runtime_stats Runtime statistics
  - @param worker_stats Worker statistics
  - @return true if export succeeded
    */
    bool export_to_csv(const std::string& filename,
    const RuntimeStats& runtime_stats,
    const std::vector<WorkerStats>& worker_stats);
    }

} // namespace stellane::runtime
