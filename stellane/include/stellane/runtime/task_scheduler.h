#pragma once

#include “stellane/runtime/runtime_config.h”
#include “stellane/core/task.h”
#include “stellane/utils/logger.h”

#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <queue>
#include <deque>
#include <chrono>
#include <unordered_map>
#include <string>
#include <optional>
#include <random>
#include <algorithm>

namespace stellane {

// Forward declarations
class Context;

// ============================================================================
// Task Scheduling Core Types
// ============================================================================

/**

- @brief Task priority levels for priority-based scheduling
  */
  enum class TaskPriority : int {
  LOWEST = 0,       ///< Background tasks, cleanup operations
  LOW = 25,         ///< Non-critical periodic tasks
  NORMAL = 50,      ///< Default priority for regular tasks
  HIGH = 75,        ///< Important user-facing operations
  HIGHEST = 100,    ///< Critical real-time tasks
  SYSTEM = 255      ///< System-level tasks (internal use)
  };

/**

- @brief Task affinity specification for worker binding
  */
  struct TaskAffinity {
  std::optional<int> preferred_worker;        ///< Preferred worker thread ID
  std::optional<int> numa_node;               ///< Preferred NUMA node
  std::string affinity_group;                 ///< Named affinity group
  bool allow_migration = true;                ///< Allow task migration between workers
  
  /**
  - @brief Create task affinity for specific worker
  - @param worker_id Target worker ID
  - @return TaskAffinity bound to worker
    */
    static TaskAffinity for_worker(int worker_id) {
    TaskAffinity affinity;
    affinity.preferred_worker = worker_id;
    affinity.allow_migration = false;
    return affinity;
    }
  
  /**
  - @brief Create task affinity for NUMA node
  - @param numa_node Target NUMA node
  - @return TaskAffinity bound to NUMA node
    */
    static TaskAffinity for_numa_node(int numa_node) {
    TaskAffinity affinity;
    affinity.numa_node = numa_node;
    return affinity;
    }
  
  /**
  - @brief Create task affinity for named group
  - @param group_name Affinity group name
  - @return TaskAffinity bound to group
    */
    static TaskAffinity for_group(const std::string& group_name) {
    TaskAffinity affinity;
    affinity.affinity_group = group_name;
    return affinity;
    }
    };

/**

- @brief Schedulable task wrapper with metadata
  */
  struct SchedulableTask {
  Task<> task;                                ///< The actual coroutine task
  TaskPriority priority = TaskPriority::NORMAL; ///< Task priority
  TaskAffinity affinity;                      ///< Worker affinity preferences
  std::chrono::steady_clock::time_point created_at; ///< Creation timestamp
  std::chrono::steady_clock::time_point scheduled_at; ///< Scheduling timestamp
  std::string name;                           ///< Optional task name for debugging
  std::string trace_id;                       ///< Associated trace ID
  size_t task_id;                             ///< Unique task identifier
  
  /**
  - @brief Create schedulable task from coroutine
  - @param t Task coroutine
  - @param prio Task priority
  - @param aff Worker affinity
  - @param task_name Optional name for debugging
    */
    SchedulableTask(Task<> t, TaskPriority prio = TaskPriority::NORMAL,
    TaskAffinity aff = {}, std::string task_name = “”)
    : task(std::move(t))
    , priority(prio)
    , affinity(std::move(aff))
    , created_at(std::chrono::steady_clock::now())
    , name(std::move(task_name))
    , task_id(generate_task_id()) {}
  
  /**
  - @brief Get task age since creation
  - @return Duration since task was created
    */
    [[nodiscard]] std::chrono::milliseconds age() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - created_at);
    }
  
  /**
  - @brief Get time since scheduling
  - @return Duration since task was scheduled
    */
    [[nodiscard]] std::chrono::milliseconds wait_time() const {
    if (scheduled_at == std::chrono::steady_clock::time_point{}) {
    return std::chrono::milliseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - scheduled_at);
    }

private:
static size_t generate_task_id() {
static std::atomic<size_t> counter{1};
return counter.fetch_add(1);
}
};

/**

- @brief Scheduler performance statistics
  */
  struct SchedulerStats {
  // Task counters
  size_t total_tasks_scheduled = 0;          ///< Total tasks ever scheduled
  size_t total_tasks_completed = 0;          ///< Total tasks completed
  size_t total_tasks_failed = 0;             ///< Total tasks that failed
  size_t current_pending_tasks = 0;          ///< Currently pending tasks
  size_t peak_pending_tasks = 0;             ///< Peak pending task count
  
  // Timing statistics  
  double average_scheduling_latency_us = 0.0; ///< Average time to schedule task (microseconds)
  double average_execution_time_ms = 0.0;    ///< Average task execution time (milliseconds)
  double average_wait_time_ms = 0.0;         ///< Average time tasks wait before execution
  
  // Load balancing (for multi-worker schedulers)
  std::vector<size_t> tasks_per_worker;      ///< Task count per worker
  std::vector<double> cpu_usage_per_worker;  ///< CPU usage per worker (%)
  size_t work_stealing_attempts = 0;         ///< Total work stealing attempts
  size_t successful_steals = 0;              ///< Successful work steals
  
  // Priority distribution
  std::unordered_map<TaskPriority, size_t> tasks_by_priority; ///< Task count by priority
  
  /**
  - @brief Calculate tasks per second
  - @return Tasks completed per second
    */
    [[nodiscard]] double tasks_per_second() const {
    // Implementation depends on timing context
    return 0.0; // Placeholder
    }
  
  /**
  - @brief Calculate work stealing success rate
  - @return Success rate as percentage (0-100)
    */
    [[nodiscard]] double work_stealing_success_rate() const {
    return work_stealing_attempts > 0 ?
    (static_cast<double>(successful_steals) * 100.0 / work_stealing_attempts) : 0.0;
    }
  
  /**
  - @brief Get load balancing efficiency
  - @return Load balance score (0-1, higher is better)
    */
    [[nodiscard]] double load_balance_score() const;
  
  /**
  - @brief Generate formatted statistics report
  - @return Human-readable statistics
    */
    [[nodiscard]] std::string to_string() const;
    };

/**

- @brief Worker thread information
  */
  struct WorkerInfo {
  int worker_id;                              ///< Worker thread identifier
  std::thread::id thread_id;                  ///< System thread ID
  int cpu_core = -1;                          ///< Bound CPU core (-1 if not bound)
  int numa_node = -1;                         ///< NUMA node (-1 if unknown)
  size_t current_task_count = 0;              ///< Currently executing tasks
  size_t total_tasks_processed = 0;           ///< Total tasks processed by this worker
  double cpu_usage_percent = 0.0;             ///< CPU usage percentage
  std::chrono::steady_clock::time_point last_activity; ///< Last activity timestamp
  bool is_stealing_enabled = true;            ///< Whether work stealing is enabled
  
  /**
  - @brief Check if worker is considered idle
  - @param idle_threshold Idle time threshold
  - @return true if worker has been idle longer than threshold
    */
    [[nodiscard]] bool is_idle(std::chrono::milliseconds idle_threshold) const {
    return std::chrono::steady_clock::now() - last_activity > idle_threshold;
    }
  
  /**
  - @brief Get worker load factor (0.0 = idle, 1.0 = fully loaded)
  - @return Load factor
    */
    [[nodiscard]] double load_factor() const {
    // Simplified calculation - real implementation would be more sophisticated
    return std::min(1.0, current_task_count / 10.0);
    }
    };

// ============================================================================
// Task Scheduler Interface
// ============================================================================

/**

- @brief Abstract interface for task scheduling strategies
- 
- This interface defines the contract for all task scheduling implementations
- in Stellane. Schedulers are responsible for deciding when and where tasks
- should be executed to optimize performance for different workload patterns.
- 
- **Thread Safety**: All methods must be thread-safe unless explicitly noted.
- **Performance**: Implementations should minimize scheduling overhead while
- maximizing throughput and maintaining low latency for high-priority tasks.
- 
- **Available Implementations**:
- - FifoScheduler: Simple first-in-first-out scheduling
- - PriorityScheduler: Priority-based task ordering
- - WorkStealingScheduler: Multi-worker load balancing
- - AffinityScheduler: CPU/NUMA-aware task placement
- - RoundRobinScheduler: Even distribution across workers
    */
    class ITaskScheduler {
    public:
    virtual ~ITaskScheduler() = default;
  
  // ========================================================================
  // Lifecycle Management
  // ========================================================================
  
  /**
  - @brief Initialize the scheduler with configuration
  - @param config Runtime configuration
  - @param worker_count Number of worker threads
  - @throws SchedulerException if initialization fails
    */
    virtual void initialize(const RuntimeConfig& config, size_t worker_count) = 0;
  
  /**
  - @brief Start the scheduler (begin processing tasks)
    */
    virtual void start() = 0;
  
  /**
  - @brief Stop the scheduler gracefully
  - @param timeout Maximum time to wait for graceful shutdown
    */
    virtual void stop(std::chrono::milliseconds timeout = std::chrono::seconds(30)) = 0;
  
  /**
  - @brief Check if scheduler is currently running
  - @return true if scheduler is active
    */
    virtual bool is_running() const = 0;
  
  // ========================================================================
  // Task Scheduling
  // ========================================================================
  
  /**
  - @brief Schedule a task for execution
  - @param task Task to schedule
  - 
  - Adds the task to the scheduling queue according to the scheduler’s
  - strategy. The task will be executed when resources become available.
    */
    virtual void schedule(SchedulableTask task) = 0;
  
  /**
  - @brief Schedule a task with specific priority
  - @param task Task coroutine
  - @param priority Task priority level
  - @param affinity Worker affinity preferences
  - @param name Optional task name for debugging
    */
    virtual void schedule_with_priority(Task<> task, TaskPriority priority,
    TaskAffinity affinity = {},
    const std::string& name = “”) {
    schedule(SchedulableTask(std::move(task), priority, std::move(affinity), name));
    }
  
  /**
  - @brief Schedule multiple tasks efficiently (batch scheduling)
  - @param tasks Vector of tasks to schedule
  - 
  - Optimized for scheduling many tasks at once, reducing overhead
  - compared to individual schedule() calls.
    */
    virtual void schedule_batch(std::vector<SchedulableTask> tasks) = 0;
  
  /**
  - @brief Try to get the next task for a specific worker
  - @param worker_id Worker requesting a task
  - @return Task to execute, or nullopt if no tasks available
  - 
  - This method is called by worker threads to get their next task.
  - The scheduler should implement its strategy here.
    */
    virtual std::optional<SchedulableTask> get_next_task(int worker_id) = 0;
  
  /**
  - @brief Notify scheduler that a task has completed
  - @param worker_id Worker that completed the task
  - @param task_id ID of completed task
  - @param execution_time Time taken to execute the task
  - @param success Whether task completed successfully
    */
    virtual void task_completed(int worker_id, size_t task_id,
    std::chrono::microseconds execution_time,
    bool success) = 0;
  
  // ========================================================================
  // Load Balancing and Work Stealing
  // ========================================================================
  
  /**
  - @brief Attempt to steal work from other workers
  - @param requesting_worker_id Worker requesting work
  - @return Stolen task, or nullopt if no work available to steal
  - 
  - Used by work-stealing schedulers to balance load across workers.
    */
    virtual std::optional<SchedulableTask> try_steal_work(int requesting_worker_id) = 0;
  
  /**
  - @brief Check if a worker should attempt work stealing
  - @param worker_id Worker to check
  - @return true if worker should try to steal work
    */
    virtual bool should_steal_work(int worker_id) const = 0;
  
  /**
  - @brief Rebalance work across all workers
  - 
  - Triggered periodically or when load imbalance is detected.
  - May move tasks between worker queues.
    */
    virtual void rebalance_load() = 0;
  
  // ========================================================================
  // Statistics and Monitoring
  // ========================================================================
  
  /**
  - @brief Get comprehensive scheduler statistics
  - @return Current scheduler performance statistics
    */
    virtual SchedulerStats get_stats() const = 0;
  
  /**
  - @brief Get information about all workers
  - @return Vector of worker information
    */
    virtual std::vector<WorkerInfo> get_worker_info() const = 0;
  
  /**
  - @brief Get current queue depths for all workers
  - @return Queue depth per worker
    */
    virtual std::vector<size_t> get_queue_depths() const = 0;
  
  /**
  - @brief Reset all performance statistics
    */
    virtual void reset_stats() = 0;
  
  /**
  - @brief Enable detailed performance profiling
  - @param enabled Whether to enable profiling
    */
    virtual void enable_profiling(bool enabled) = 0;
  
  // ========================================================================
  // Configuration and Tuning
  // ========================================================================
  
  /**
  - @brief Update scheduler configuration (hot reload)
  - @param config New configuration
  - @return true if configuration was successfully applied
    */
    virtual bool update_config(const RuntimeConfig& config) = 0;
  
  /**
  - @brief Get current scheduler configuration
  - @return Current configuration
    */
    virtual RuntimeConfig get_config() const = 0;
  
  /**
  - @brief Set CPU affinity for workers
  - @param worker_cpu_mapping Map of worker ID to CPU core
  - @return true if affinity was successfully set
    */
    virtual bool set_worker_affinity(const std::unordered_map<int, int>& worker_cpu_mapping) = 0;
  
  /**
  - @brief Create an affinity group for related tasks
  - @param group_name Name of the affinity group
  - @param preferred_workers List of preferred worker IDs for this group
    */
    virtual void create_affinity_group(const std::string& group_name,
    const std::vector<int>& preferred_workers) = 0;
  
  // ========================================================================
  // Advanced Features
  // ========================================================================
  
  /**
  - @brief Set custom scheduling hint for task placement
  - @param task_pattern Pattern to match task names/types
  - @param hint Custom scheduling hint
    */
    virtual void set_scheduling_hint(const std::string& task_pattern,
    const std::unordered_map<std::string, std::string>& hint) = 0;
  
  /**
  - @brief Register a custom load balancing strategy
  - @param strategy_name Name of the strategy
  - @param strategy Load balancing function
    */
    virtual void register_load_balancing_strategy(const std::string& strategy_name,
    std::function<void(std::vector<WorkerInfo>&)> strategy) = 0;
  
  /**
  - @brief Pause scheduling for a specific worker
  - @param worker_id Worker to pause
  - @param reason Reason for pausing (for logging)
    */
    virtual void pause_worker(int worker_id, const std::string& reason = “”) = 0;
  
  /**
  - @brief Resume scheduling for a paused worker
  - @param worker_id Worker to resume
    */
    virtual void resume_worker(int worker_id) = 0;
  
  /**
  - @brief Get scheduler type information
  - @return Scheduler type and version info
    */
    virtual std::string get_scheduler_info() const = 0;
    };

// ============================================================================
// Scheduler Factory and Registry
// ============================================================================

/**

- @brief Factory for creating task scheduler instances
  */
  class TaskSchedulerFactory {
  public:
  /**
  - @brief Scheduler creation function signature
    */
    using SchedulerFactory = std::function<std::unique_ptr<ITaskScheduler>()>;
  
  /**
  - @brief Create a scheduler of the specified type
  - @param strategy Scheduling strategy type
  - @param config Runtime configuration
  - @param worker_count Number of worker threads
  - @return Unique pointer to created scheduler
  - @throws SchedulerException if creation fails
    */
    static std::unique_ptr<ITaskScheduler> create(TaskSchedulingStrategy strategy,
    const RuntimeConfig& config,
    size_t worker_count);
  
  /**
  - @brief Register a custom scheduler factory
  - @param name Scheduler name
  - @param factory Scheduler creation function
    */
    static void register_scheduler(const std::string& name, SchedulerFactory factory);
  
  /**
  - @brief Get list of available scheduler names
  - @return Vector of registered scheduler names
    */
    static std::vector<std::string> get_available_schedulers();
  
  /**
  - @brief Get recommended scheduler for workload type
  - @param cpu_intensive Whether workload is CPU-intensive
  - @param io_intensive Whether workload is I/O-intensive
  - @param real_time Whether workload requires real-time guarantees
  - @return Recommended scheduling strategy
    */
    static TaskSchedulingStrategy get_recommended_scheduler(bool cpu_intensive = false,
    bool io_intensive = false,
    bool real_time = false);

private:
static std::unordered_map<std::string, SchedulerFactory> factories_;
static std::mutex factories_mutex_;
};

// ============================================================================
// Scheduler Exceptions
// ============================================================================

/**

- @brief Base exception for scheduler-related errors
  */
  class SchedulerException : public std::exception {
  public:
  explicit SchedulerException(const std::string& message) : message_(message) {}
  const char* what() const noexcept override { return message_.c_str(); }

private:
std::string message_;
};

/**

- @brief Exception thrown when scheduler initialization fails
  */
  class SchedulerInitializationException : public SchedulerException {
  public:
  SchedulerInitializationException(const std::string& scheduler, const std::string& reason)
  : SchedulerException(“Failed to initialize “ + scheduler + “ scheduler: “ + reason) {}
  };

/**

- @brief Exception thrown when task scheduling fails
  */
  class TaskSchedulingException : public SchedulerException {
  public:
  TaskSchedulingException(const std::string& task_name, const std::string& reason)
  : SchedulerException(“Failed to schedule task ‘” + task_name + “’: “ + reason) {}
  };

/**

- @brief Exception thrown when worker operations fail
  */
  class WorkerException : public SchedulerException {
  public:
  WorkerException(int worker_id, const std::string& operation, const std::string& reason)
  : SchedulerException(“Worker “ + std::to_string(worker_id) + “ “ + operation + “ failed: “ + reason) {}
  };

// ============================================================================
// Base Scheduler Implementation
// ============================================================================

/**

- @brief Base implementation providing common functionality for task schedulers
- 
- This class provides default implementations for common operations and
- utility methods that most schedulers can reuse. Concrete schedulers should
- inherit from this class and override the pure virtual methods.
  */
  class BaseTaskScheduler : public ITaskScheduler {
  public:
  explicit BaseTaskScheduler(const std::string& scheduler_name);
  virtual ~BaseTaskScheduler();
  
  // ITaskScheduler interface - common implementations
  void initialize(const RuntimeConfig& config, size_t worker_count) override;
  void start() override;
  void stop(std::chrono::milliseconds timeout) override;
  bool is_running() const override;
  
  void schedule_batch(std::vector<SchedulableTask> tasks) override;
  void task_completed(int worker_id, size_t task_id,
  std::chrono::microseconds execution_time, bool success) override;
  
  // Statistics and monitoring
  SchedulerStats get_stats() const override;
  std::vector<WorkerInfo> get_worker_info() const override;
  void reset_stats() override;
  void enable_profiling(bool enabled) override;
  
  // Configuration
  bool update_config(const RuntimeConfig& config) override;
  RuntimeConfig get_config() const override;
  bool set_worker_affinity(const std::unordered_map<int, int>& worker_cpu_mapping) override;
  void create_affinity_group(const std::string& group_name,
  const std::vector<int>& preferred_workers) override;
  
  // Advanced features
  void set_scheduling_hint(const std::string& task_pattern,
  const std::unordered_map<std::string, std::string>& hint) override;
  void register_load_balancing_strategy(const std::string& strategy_name,
  std::function<void(std::vector<WorkerInfo>&)> strategy) override;
  void pause_worker(int worker_id, const std::string& reason) override;
  void resume_worker(int worker_id) override;
  std::string get_scheduler_info() const override;
  
  // Default implementations for optional features
  std::optional<SchedulableTask> try_steal_work(int requesting_worker_id) override {
  return std::nullopt; // Base implementation doesn’t support work stealing
  }
  
  bool should_steal_work(int worker_id) const override {
  return false; // Base implementation doesn’t support work stealing
  }
  
  void rebalance_load() override {
  // Base implementation does nothing
  }

protected:
// Protected interface for derived classes

```
/**
 * @brief Get the logger instance
 * @return Reference to the scheduler logger
 */
Logger& logger() { return *logger_; }

/**
 * @brief Get worker information by ID
 * @param worker_id Worker identifier
 * @return Worker information
 */
const WorkerInfo& get_worker(int worker_id) const;

/**
 * @brief Update worker statistics
 * @param worker_id Worker identifier
 * @param delta_tasks Number of tasks processed
 * @param delta_time Time spent processing
 */
void update_worker_stats(int worker_id, size_t delta_tasks, 
                        std::chrono::microseconds delta_time);

/**
 * @brief Check if worker is paused
 * @param worker_id Worker identifier
 * @return true if worker is paused
 */
bool is_worker_paused(int worker_id) const;

/**
 * @brief Get affinity group preferences for a worker
 * @param worker_id Worker identifier
 * @return List of affinity groups this worker handles
 */
std::vector<std::string> get_worker_affinity_groups(int worker_id) const;

/**
 * @brief Check if a task matches worker affinity
 * @param task Task to check
 * @param worker_id Target worker
 * @return true if task is suitable for worker
 */
bool matches_worker_affinity(const SchedulableTask& task, int worker_id) const;
```

private:
// Configuration and state
std::string scheduler_name_;
RuntimeConfig config_;
std::shared_ptr<Logger> logger_;
std::atomic<bool> running_{false};
std::atomic<bool> stop_requested_{false};
size_t worker_count_ = 0;

```
// Worker management
std::vector<WorkerInfo> workers_;
std::unordered_map<int, int> worker_cpu_mapping_;
std::unordered_map<std::string, std::vector<int>> affinity_groups_;
std::unordered_set<int> paused_workers_;
mutable std::shared_mutex workers_mutex_;

// Statistics
mutable SchedulerStats stats_;
mutable std::mutex stats_mutex_;
bool profiling_enabled_ = false;
std::chrono::steady_clock::time_point start_time_;

// Load balancing strategies
std::unordered_map<std::string, std::function<void(std::vector<WorkerInfo>&)>> load_balancing_strategies_;

// Scheduling hints
std::unordered_map<std::string, std::unordered_map<std::string, std::string>> scheduling_hints_;

// Helper methods
void initialize_workers();
void initialize_logger();
void cleanup_resources();
void update_global_stats();
```

};

// ============================================================================
// Utility Functions
// ============================================================================

/**

- @brief Convert task priority enum to string
- @param priority Task priority
- @return String representation
  */
  [[nodiscard]] std::string to_string(TaskPriority priority);

/**

- @brief Parse task priority from string
- @param priority_str String representation
- @return TaskPriority enum or std::nullopt if invalid
  */
  [[nodiscard]] std::optional<TaskPriority> priority_from_string(const std::string& priority_str);

/**

- @brief Convert scheduling strategy enum to string
- @param strategy Scheduling strategy
- @return String representation
  */
  [[nodiscard]] std::string to_string(TaskSchedulingStrategy strategy);

/**

- @brief Calculate optimal worker count for current hardware
- @param cpu_intensive Whether workload is CPU-intensive
- @param io_intensive Whether workload is I/O-intensive
- @return Recommended worker count
  */
  [[nodiscard]] size_t calculate_optimal_worker_count(bool cpu_intensive = false,
  bool io_intensive = false);

/**

- @brief Get current system NUMA topology
- @return Map of NUMA node to CPU cores
  */
  [[nodiscard]] std::unordered_map<int, std::vector<int>> get_numa_topology();

/**

- @brief Check if work stealing would be beneficial
- @param workers Current worker information
- @param threshold Load imbalance threshold
- @return true if work stealing should be attempted
  */
  [[nodiscard]] bool should_attempt_work_stealing(const std::vector<WorkerInfo>& workers,
  double threshold = 0.3);

} // namespace stellane
