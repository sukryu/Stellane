#include “stellane/runtime/task_scheduler.h”
#include “stellane/runtime/runtime_config.h”
#include “stellane/runtime/runtime_stats.h”
#include “stellane/utils/logger.h”

#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <cassert>

// Platform-specific includes for CPU affinity and NUMA
#ifdef **linux**
#include <sched.h>
#include <numa.h>
#include <unistd.h>
#include <sys/syscall.h>
#elif defined(_WIN32)
#include <windows.h>
#include <processthreadsapi.h>
#elif defined(**APPLE**)
#include <mach/thread_policy.h>
#include <mach/mach.h>
#endif

namespace stellane {

// ============================================================================
// Utility Functions Implementation
// ============================================================================

std::string to_string(TaskPriority priority) {
switch (priority) {
case TaskPriority::LOWEST: return “lowest”;
case TaskPriority::LOW: return “low”;
case TaskPriority::NORMAL: return “normal”;
case TaskPriority::HIGH: return “high”;
case TaskPriority::HIGHEST: return “highest”;
case TaskPriority::SYSTEM: return “system”;
default: return “unknown”;
}
}

std::optional<TaskPriority> priority_from_string(const std::string& priority_str) {
if (priority_str == “lowest”) return TaskPriority::LOWEST;
if (priority_str == “low”) return TaskPriority::LOW;
if (priority_str == “normal”) return TaskPriority::NORMAL;
if (priority_str == “high”) return TaskPriority::HIGH;
if (priority_str == “highest”) return TaskPriority::HIGHEST;
if (priority_str == “system”) return TaskPriority::SYSTEM;
return std::nullopt;
}

std::string to_string(TaskSchedulingStrategy strategy) {
switch (strategy) {
case TaskSchedulingStrategy::FIFO: return “fifo”;
case TaskSchedulingStrategy::PRIORITY: return “priority”;
case TaskSchedulingStrategy::WORK_STEALING: return “work_stealing”;
case TaskSchedulingStrategy::AFFINITY: return “affinity”;
case TaskSchedulingStrategy::ROUND_ROBIN: return “round_robin”;
case TaskSchedulingStrategy::CUSTOM: return “custom”;
default: return “unknown”;
}
}

size_t calculate_optimal_worker_count(bool cpu_intensive, bool io_intensive) {
size_t hardware_cores = std::thread::hardware_concurrency();
if (hardware_cores == 0) hardware_cores = 1;

```
if (cpu_intensive && !io_intensive) {
    // CPU-bound: use physical cores
    return hardware_cores;
} else if (io_intensive && !cpu_intensive) {
    // I/O-bound: can use more workers
    return hardware_cores * 2;
} else if (cpu_intensive && io_intensive) {
    // Mixed workload: balanced approach
    return static_cast<size_t>(hardware_cores * 1.5);
} else {
    // General purpose: slightly less than hardware cores
    return std::max(1UL, hardware_cores - 1);
}
```

}

std::unordered_map<int, std::vector<int>> get_numa_topology() {
std::unordered_map<int, std::vector<int>> topology;

#ifdef **linux**
if (numa_available() != -1) {
int max_node = numa_max_node();

```
    for (int node = 0; node <= max_node; ++node) {
        struct bitmask* cpus = numa_allocate_cpumask();
        if (numa_node_to_cpus(node, cpus) == 0) {
            std::vector<int> cpu_list;
            
            for (unsigned int cpu = 0; cpu < cpus->size; ++cpu) {
                if (numa_bitmask_isbitset(cpus, cpu)) {
                    cpu_list.push_back(static_cast<int>(cpu));
                }
            }
            
            if (!cpu_list.empty()) {
                topology[node] = std::move(cpu_list);
            }
        }
        numa_free_cpumask(cpus);
    }
}
```

#endif

```
// Fallback: assume single NUMA node with all CPUs
if (topology.empty()) {
    std::vector<int> all_cpus;
    for (int i = 0; i < static_cast<int>(std::thread::hardware_concurrency()); ++i) {
        all_cpus.push_back(i);
    }
    topology[0] = std::move(all_cpus);
}

return topology;
```

}

bool should_attempt_work_stealing(const std::vector<WorkerInfo>& workers, double threshold) {
if (workers.size() < 2) return false;

```
// Calculate load variance
double min_load = 1.0, max_load = 0.0;
for (const auto& worker : workers) {
    double load = worker.load_factor();
    min_load = std::min(min_load, load);
    max_load = std::max(max_load, load);
}

return (max_load - min_load) > threshold;
```

}

// ============================================================================
// SchedulerStats Implementation
// ============================================================================

double SchedulerStats::load_balance_score() const {
if (tasks_per_worker.empty()) return 1.0;

```
// Calculate coefficient of variation (lower is better)
double mean = 0.0;
for (size_t count : tasks_per_worker) {
    mean += count;
}
mean /= tasks_per_worker.size();

if (mean == 0.0) return 1.0;

double variance = 0.0;
for (size_t count : tasks_per_worker) {
    double diff = static_cast<double>(count) - mean;
    variance += diff * diff;
}
variance /= tasks_per_worker.size();

double cv = std::sqrt(variance) / mean;
return std::max(0.0, 1.0 - cv); // Higher score = better balance
```

}

std::string SchedulerStats::to_string() const {
std::ostringstream oss;
oss << “Scheduler Statistics:\n”;
oss << “  Total Scheduled: “ << total_tasks_scheduled << “\n”;
oss << “  Total Completed: “ << total_tasks_completed << “\n”;
oss << “  Total Failed: “ << total_tasks_failed << “\n”;
oss << “  Currently Pending: “ << current_pending_tasks << “\n”;
oss << “  Peak Pending: “ << peak_pending_tasks << “\n”;
oss << “  Avg Scheduling Latency: “ << std::fixed << std::setprecision(2)
<< average_scheduling_latency_us << “μs\n”;
oss << “  Avg Execution Time: “ << std::fixed << std::setprecision(2)
<< average_execution_time_ms << “ms\n”;
oss << “  Work Stealing Success: “ << std::fixed << std::setprecision(1)
<< work_stealing_success_rate() << “%\n”;
oss << “  Load Balance Score: “ << std::fixed << std::setprecision(3)
<< load_balance_score() << “\n”;

```
if (!tasks_per_worker.empty()) {
    oss << "  Tasks per Worker: [";
    for (size_t i = 0; i < tasks_per_worker.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << tasks_per_worker[i];
    }
    oss << "]\n";
}

return oss.str();
```

}

// ============================================================================
// BaseTaskScheduler Implementation
// ============================================================================

BaseTaskScheduler::BaseTaskScheduler(const RuntimeConfig& config)
: config_(config)
, worker_count_(config.worker.worker_threads)
, next_task_id_(1)
, running_(false)
, shutdown_requested_(false) {

```
initialize_logger();

// Initialize worker information
workers_.resize(worker_count_);
for (size_t i = 0; i < worker_count_; ++i) {
    workers_[i].worker_id = static_cast<int>(i);
    workers_[i].last_activity = std::chrono::steady_clock::now();
}

// Initialize statistics
stats_.tasks_per_worker.resize(worker_count_, 0);
stats_.cpu_usage_per_worker.resize(worker_count_, 0.0);

logger_->info("BaseTaskScheduler initialized with {} workers", worker_count_);
```

}

BaseTaskScheduler::~BaseTaskScheduler() {
if (running_.load()) {
logger_->warn(“TaskScheduler destroyed while running - forcing shutdown”);
shutdown();
}

```
logger_->info("BaseTaskScheduler destroyed");
```

}

void BaseTaskScheduler::initialize_logger() {
logger_ = std::make_shared<Logger>(“stellane.scheduler”);

```
if (config_.logging.enable_trace_logging) {
    logger_->set_level(Logger::Level::TRACE);
}
```

}

// ============================================================================
// ITaskScheduler Interface Implementation
// ============================================================================

void BaseTaskScheduler::start() {
if (running_.exchange(true)) {
throw SchedulerInitializationException(“BaseTaskScheduler”, “Already running”);
}

```
shutdown_requested_.store(false);

logger_->info("Starting task scheduler with {} workers", worker_count_);

// Initialize worker threads
worker_threads_.clear();
worker_threads_.reserve(worker_count_);

for (size_t i = 0; i < worker_count_; ++i) {
    worker_threads_.emplace_back(&BaseTaskScheduler::worker_main, this, static_cast<int>(i));
    workers_[i].thread_id = worker_threads_.back().get_id();
    
    // Set CPU affinity if enabled
    if (config_.performance.enable_cpu_affinity) {
        set_worker_cpu_affinity(static_cast<int>(i));
    }
}

// Start statistics thread
stats_thread_ = std::thread(&BaseTaskScheduler::stats_main, this);

logger_->info("Task scheduler started successfully");
```

}

void BaseTaskScheduler::shutdown() {
if (!running_.load()) {
return;
}

```
logger_->info("Shutting down task scheduler");

shutdown_requested_.store(true);

// Wake up all workers
{
    std::unique_lock lock(queue_mutex_);
    queue_cv_.notify_all();
}

// Wait for workers to finish
for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
        thread.join();
    }
}

// Stop statistics thread
if (stats_thread_.joinable()) {
    stats_thread_.join();
}

running_.store(false);

logger_->info("Task scheduler shutdown complete");
```

}

void BaseTaskScheduler::schedule_task(Task<> task, int priority, const TaskAffinity& affinity) {
if (!task) {
throw TaskSchedulingException(“null_task”, “Task cannot be null”);
}

```
if (!running_.load()) {
    throw TaskSchedulingException("scheduler_not_running", "Scheduler is not running");
}

auto task_id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
auto schedule_time = std::chrono::steady_clock::now();

SchedulableTask schedulable_task{
    .task = std::move(task),
    .task_id = task_id,
    .priority = static_cast<TaskPriority>(std::clamp(priority, 0, 255)),
    .affinity = affinity,
    .scheduled_time = schedule_time,
    .worker_id = -1  // Will be assigned by scheduler
};

// Use derived class strategy to place the task
schedule_task_impl(std::move(schedulable_task));

// Update statistics
{
    std::unique_lock lock(stats_mutex_);
    stats_.total_tasks_scheduled++;
    stats_.current_pending_tasks++;
    if (stats_.current_pending_tasks > stats_.peak_pending_tasks) {
        stats_.peak_pending_tasks = stats_.current_pending_tasks;
    }
}

if (config_.logging.log_task_lifecycle) {
    logger_->trace("Task {} scheduled with priority {}", task_id, priority);
}
```

}

void BaseTaskScheduler::schedule_batch(std::vector<SchedulableTask> tasks) {
if (tasks.empty()) return;

```
if (!running_.load()) {
    throw TaskSchedulingException("batch_schedule", "Scheduler is not running");
}

// Assign task IDs and timestamps
auto schedule_time = std::chrono::steady_clock::now();
for (auto& task : tasks) {
    task.task_id = next_task_id_.fetch_add(1, std::memory_order_relaxed);
    task.scheduled_time = schedule_time;
}

// Use derived class strategy for batch scheduling
schedule_batch_impl(std::move(tasks));

// Update statistics
{
    std::unique_lock lock(stats_mutex_);
    stats_.total_tasks_scheduled += tasks.size();
    stats_.current_pending_tasks += tasks.size();
    if (stats_.current_pending_tasks > stats_.peak_pending_tasks) {
        stats_.peak_pending_tasks = stats_.current_pending_tasks;
    }
}

logger_->trace("Batch of {} tasks scheduled", tasks.size());
```

}

void BaseTaskScheduler::task_completed(int worker_id, size_t task_id,
std::chrono::microseconds execution_time,
bool success) {
// Update worker information
if (worker_id >= 0 && worker_id < static_cast<int>(workers_.size())) {
auto& worker = workers_[worker_id];
worker.total_tasks_processed++;
worker.last_activity = std::chrono::steady_clock::now();
worker.current_task_count = std::max(1UL, worker.current_task_count) - 1;
}

```
// Update global statistics
{
    std::unique_lock lock(stats_mutex_);
    
    if (success) {
        stats_.total_tasks_completed++;
    } else {
        stats_.total_tasks_failed++;
    }
    
    if (stats_.current_pending_tasks > 0) {
        stats_.current_pending_tasks--;
    }
    
    // Update moving averages
    double exec_time_ms = execution_time.count() / 1000.0;
    double alpha = 0.1; // Smoothing factor
    stats_.average_execution_time_ms = 
        alpha * exec_time_ms + (1.0 - alpha) * stats_.average_execution_time_ms;
    
    // Update per-worker statistics
    if (worker_id >= 0 && worker_id < static_cast<int>(stats_.tasks_per_worker.size())) {
        stats_.tasks_per_worker[worker_id]++;
    }
}

// Record in global stats collector
record_task_execution(worker_id, execution_time.count() / 1000.0, success);

if (config_.logging.log_task_lifecycle) {
    logger_->trace("Task {} completed by worker {} in {:.3f}ms ({})",
                  task_id, worker_id, exec_time_ms, success ? "success" : "failed");
}
```

}

SchedulerStats BaseTaskScheduler::get_stats() const {
std::shared_lock lock(stats_mutex_);
return stats_;
}

std::vector<WorkerInfo> BaseTaskScheduler::get_worker_info() const {
std::shared_lock lock(workers_mutex_);
return workers_;
}

void BaseTaskScheduler::reset_stats() {
std::unique_lock lock(stats_mutex_);

```
auto start_time = stats_.start_time;
stats_ = SchedulerStats{};
stats_.start_time = start_time;
stats_.tasks_per_worker.resize(worker_count_, 0);
stats_.cpu_usage_per_worker.resize(worker_count_, 0.0);

logger_->info("Scheduler statistics reset");
```

}

bool BaseTaskScheduler::update_config(const RuntimeConfig& config) {
// Update non-critical configuration
config_.performance.idle_timeout = config.performance.idle_timeout;
config_.logging = config.logging;

```
if (config.logging.enable_trace_logging != config_.logging.enable_trace_logging) {
    if (config.logging.enable_trace_logging) {
        logger_->set_level(Logger::Level::TRACE);
    } else {
        logger_->set_level(Logger::Level::INFO);
    }
}

logger_->info("Scheduler configuration updated");
return true;
```

}

std::string BaseTaskScheduler::get_scheduler_info() const {
std::ostringstream oss;
oss << “BaseTaskScheduler:\n”;
oss << “  Strategy: “ << to_string(config_.strategy) << “\n”;
oss << “  Workers: “ << worker_count_ << “\n”;
oss << “  Running: “ << (running_.load() ? “yes” : “no”) << “\n”;
oss << “  CPU Affinity: “ << (config_.performance.enable_cpu_affinity ? “enabled” : “disabled”) << “\n”;
oss << “  NUMA Aware: “ << (config_.performance.numa_aware ? “enabled” : “disabled”) << “\n”;
return oss.str();
}

// ============================================================================
// Worker Thread Management
// ============================================================================

void BaseTaskScheduler::worker_main(int worker_id) {
logger_->trace(“Worker {} started”, worker_id);

```
// Update worker thread info
{
    std::unique_lock lock(workers_mutex_);
    workers_[worker_id].thread_id = std::this_thread::get_id();
}

// Set thread name for debugging
set_worker_thread_name(worker_id);

while (!shutdown_requested_.load()) {
    try {
        // Try to get a task
        auto task_opt = get_next_task(worker_id);
        
        if (task_opt) {
            execute_task(worker_id, std::move(*task_opt));
        } else {
            // No task available, try work stealing
            if (config_.worker.work_stealing.enabled && should_steal_work(worker_id)) {
                auto stolen_task = try_steal_work(worker_id);
                if (stolen_task) {
                    execute_task(worker_id, std::move(*stolen_task));
                    
                    // Update work stealing statistics
                    {
                        std::unique_lock lock(stats_mutex_);
                        stats_.successful_steals++;
                    }
                    continue;
                }
                
                // Update work stealing attempts
                {
                    std::unique_lock lock(stats_mutex_);
                    stats_.work_stealing_attempts++;
                }
            }
            
            // No work available, wait
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait_for(lock, config_.performance.idle_timeout,
                [this] { return shutdown_requested_.load() || has_work_available(); });
        }
        
    } catch (const std::exception& e) {
        logger_->error("Worker {} error: {}", worker_id, e.what());
        
        // Brief pause to prevent tight error loops
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

logger_->trace("Worker {} stopped", worker_id);
```

}

void BaseTaskScheduler::execute_task(int worker_id, SchedulableTask schedulable_task) {
auto start_time = std::chrono::steady_clock::now();

```
// Update worker state
{
    std::unique_lock lock(workers_mutex_);
    workers_[worker_id].current_task_count++;
    workers_[worker_id].last_activity = start_time;
}

bool success = false;
try {
    // Execute the coroutine task
    schedulable_task.task.resume();
    success = true;
    
} catch (const std::exception& e) {
    logger_->error("Task {} execution failed: {}", schedulable_task.task_id, e.what());
    success = false;
}

auto end_time = std::chrono::steady_clock::now();
auto execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
    end_time - start_time);

// Record task completion
task_completed(worker_id, schedulable_task.task_id, execution_time, success);
```

}

void BaseTaskScheduler::set_worker_cpu_affinity(int worker_id) {
if (!config_.performance.enable_cpu_affinity) {
return;
}

#ifdef **linux**
// Get NUMA topology if NUMA-aware
if (config_.performance.numa_aware) {
auto topology = get_numa_topology();

```
    // Assign worker to specific NUMA node and CPU
    int numa_node = worker_id % topology.size();
    auto& cpus = topology[numa_node];
    
    if (!cpus.empty()) {
        int cpu_core = cpus[worker_id % cpus.size()];
        
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        
        if (pthread_setaffinity_np(worker_threads_[worker_id].native_handle(),
                                  sizeof(cpu_set_t), &cpuset) == 0) {
            workers_[worker_id].cpu_core = cpu_core;
            workers_[worker_id].numa_node = numa_node;
            
            logger_->trace("Worker {} bound to CPU {} (NUMA node {})",
                          worker_id, cpu_core, numa_node);
        } else {
            logger_->warn("Failed to set CPU affinity for worker {}", worker_id);
        }
    }
} else {
    // Simple round-robin CPU assignment
    int cpu_core = worker_id % std::thread::hardware_concurrency();
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    
    if (pthread_setaffinity_np(worker_threads_[worker_id].native_handle(),
                              sizeof(cpu_set_t), &cpuset) == 0) {
        workers_[worker_id].cpu_core = cpu_core;
        logger_->trace("Worker {} bound to CPU {}", worker_id, cpu_core);
    }
}
```

#elif defined(*WIN32)
// Windows CPU affinity
DWORD_PTR cpu_mask = 1ULL << (worker_id % std::thread::hardware_concurrency());
if (SetThreadAffinityMask(worker_threads*[worker_id].native_handle(), cpu_mask)) {
workers_[worker_id].cpu_core = worker_id % std::thread::hardware_concurrency();
logger_->trace(“Worker {} bound to CPU {}”, worker_id, workers_[worker_id].cpu_core);
}

#elif defined(**APPLE**)
// macOS thread affinity (limited support)
thread_affinity_policy_data_t policy = { worker_id % std::thread::hardware_concurrency() };

```
if (thread_policy_set(pthread_mach_thread_np(worker_threads_[worker_id].native_handle()),
                     THREAD_AFFINITY_POLICY,
                     (thread_policy_t)&policy,
                     THREAD_AFFINITY_POLICY_COUNT) == KERN_SUCCESS) {
    workers_[worker_id].cpu_core = worker_id % std::thread::hardware_concurrency();
    logger_->trace("Worker {} affinity set to CPU {}", worker_id, workers_[worker_id].cpu_core);
}
```

#endif
}

void BaseTaskScheduler::set_worker_thread_name(int worker_id) {
std::string name = “stellane-worker-” + std::to_string(worker_id);

#ifdef **linux**
pthread_setname_np(pthread_self(), name.c_str());
#elif defined(**APPLE**)
pthread_setname_np(name.c_str());
#elif defined(_WIN32)
// Windows doesn’t have a direct equivalent, but we can use SetThreadDescription (Windows 10+)
std::wstring wide_name(name.begin(), name.end());
SetThreadDescription(GetCurrentThread(), wide_name.c_str());
#endif
}

// ============================================================================
// Statistics and Monitoring
// ============================================================================

void BaseTaskScheduler::stats_main() {
logger_->trace(“Statistics thread started”);

```
while (!shutdown_requested_.load()) {
    update_worker_statistics();
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
}

logger_->trace("Statistics thread stopped");
```

}

void BaseTaskScheduler::update_worker_statistics() {
auto now = std::chrono::steady_clock::now();

```
std::unique_lock workers_lock(workers_mutex_);
std::unique_lock stats_lock(stats_mutex_);

for (size_t i = 0; i < workers_.size(); ++i) {
    auto& worker = workers_[i];
    
    // Update CPU usage (simplified calculation)
    // In a real implementation, this would use platform-specific APIs
    if (worker.current_task_count > 0) {
        worker.cpu_usage_percent = 75.0 + (worker.current_task_count * 5.0);
    } else {
        auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
            now - worker.last_activity).count();
        worker.cpu_usage_percent = std::max(0.0, 10.0 - idle_time);
    }
    
    worker.cpu_usage_percent = std::min(100.0, worker.cpu_usage_percent);
    
    // Update statistics
    if (i < stats_.cpu_usage_per_worker.size()) {
        stats_.cpu_usage_per_worker[i] = worker.cpu_usage_percent;
    }
}
```

}

// ============================================================================
// Default Implementation Stubs (To be overridden by concrete schedulers)
// ============================================================================

void BaseTaskScheduler::schedule_task_impl(SchedulableTask task) {
// Base implementation: simple FIFO scheduling
// Concrete schedulers should override this

```
std::unique_lock lock(queue_mutex_);
task_queue_.push_back(std::move(task));
queue_cv_.notify_one();
```

}

void BaseTaskScheduler::schedule_batch_impl(std::vector<SchedulableTask> tasks) {
// Base implementation: schedule one by one
// Concrete schedulers can optimize this

```
std::unique_lock lock(queue_mutex_);
for (auto& task : tasks) {
    task_queue_.push_back(std::move(task));
}
queue_cv_.notify_all();
```

}

std::optional<SchedulableTask> BaseTaskScheduler::get_next_task(int worker_id) {
// Base implementation: simple FIFO
// Concrete schedulers should override this

```
std::unique_lock lock(queue_mutex_);

if (task_queue_.empty()) {
    return std::nullopt;
}

auto task = std::move(task_queue_.front());
task_queue_.pop_front();
task.worker_id = worker_id;

return task;
```

}

bool BaseTaskScheduler::has_work_available() const {
// Base implementation
return !task_queue_.empty();
}

// ============================================================================
// FIFO Scheduler Implementation
// ============================================================================

class FIFOScheduler : public BaseTaskScheduler {
public:
explicit FIFOScheduler(const RuntimeConfig& config)
: BaseTaskScheduler(config) {}

```
std::string get_scheduler_info() const override {
    std::ostringstream oss;
    oss << "FIFOScheduler:\n";
    oss << "  Strategy: FIFO (First-In-First-Out)\n";
    oss << "  Workers: " << worker_count_ << "\n";
    oss << "  Running: " << (running_.load() ? "yes" : "no") << "\n";
    oss << "  Queue Size: " << task_queue_.size() << "\n";
    return oss.str();
}

// Use base implementation for FIFO scheduling
```

};

// ============================================================================
// Priority Scheduler Implementation
// ============================================================================

```
std::optional<SchedulableTask> get_next_task(int worker_id) override {
    std::unique_lock lock(priority_queue_mutex_);
    
    if (priority_queue_.empty()) {
        return std::nullopt;
    }
    
    auto task = std::move(const_cast<SchedulableTask&>(priority_queue_.top()));
    priority_queue_.pop();
    task.worker_id = worker_id;
    
    return task;
}

bool has_work_available() const override {
    std::shared_lock lock(priority_queue_mutex_);
    return !priority_queue_.empty();
}

std::string get_scheduler_info() const override {
    std::ostringstream oss;
    oss << "PriorityScheduler:\n";
    oss << "  Strategy: Priority-based scheduling\n";
    oss << "  Workers: " << worker_count_ << "\n";
    oss << "  Running: " << (running_.load() ? "yes" : "no") << "\n";
    
    {
        std::shared_lock lock(priority_queue_mutex_);
        oss << "  Queue Size: " << priority_queue_.size() << "\n";
    }
    
    return oss.str();
}
```

};

// ============================================================================
// Work-Stealing Scheduler Implementation
// ============================================================================

class WorkStealingScheduler : public BaseTaskScheduler {
private:
// Per-worker task deques for work stealing
std::vector<std::deque<SchedulableTask>> worker_queues_;
mutable std::vector<std::shared_mutex> worker_queue_mutexes_;

```
// Random number generator for stealing attempts
mutable std::random_device rd_;
mutable std::mt19937 rng_;

// Work stealing configuration
std::chrono::microseconds steal_interval_;
size_t steal_threshold_;
```

public:
explicit WorkStealingScheduler(const RuntimeConfig& config)
: BaseTaskScheduler(config)
, worker_queues_(config.worker.worker_threads)
, worker_queue_mutexes_(config.worker.worker_threads)
, rng_(rd_())
, steal_interval_(config.worker.work_stealing.steal_interval)
, steal_threshold_(config.worker.work_stealing.steal_threshold) {}

protected:
void schedule_task_impl(SchedulableTask task) override {
// Choose worker with least load
int target_worker = choose_target_worker();

```
    {
        std::unique_lock lock(worker_queue_mutexes_[target_worker]);
        worker_queues_[target_worker].push_back(std::move(task));
    }
    
    queue_cv_.notify_one();
}

void schedule_batch_impl(std::vector<SchedulableTask> tasks) override {
    // Distribute tasks across workers
    for (size_t i = 0; i < tasks.size(); ++i) {
        int target_worker = i % worker_count_;
        
        {
            std::unique_lock lock(worker_queue_mutexes_[target_worker]);
            worker_queues_[target_worker].push_back(std::move(tasks[i]));
        }
    }
    
    queue_cv_.notify_all();
}

std::optional<SchedulableTask> get_next_task(int worker_id) override {
    // First, try local queue
    {
        std::unique_lock lock(worker_queue_mutexes_[worker_id]);
        if (!worker_queues_[worker_id].empty()) {
            auto task = std::move(worker_queues_[worker_id].front());
            worker_queues_[worker_id].pop_front();
            task.worker_id = worker_id;
            return task;
        }
    }
    
    // Local queue is empty, return nullopt (work stealing handled separately)
    return std::nullopt;
}

std::optional<SchedulableTask> try_steal_work(int requesting_worker_id) override {
    if (worker_count_ <= 1) {
        return std::nullopt;
    }
    
    // Generate random worker order for stealing attempts
    std::vector<int> steal_order;
    for (int i = 0; i < static_cast<int>(worker_count_); ++i) {
        if (i != requesting_worker_id) {
            steal_order.push_back(i);
        }
    }
    
    std::shuffle(steal_order.begin(), steal_order.end(), rng_);
    
    // Try to steal from multiple workers
    for (int victim_worker : steal_order) {
        std::unique_lock lock(worker_queue_mutexes_[victim_worker], std::try_to_lock);
        
        if (lock.owns_lock() && 
            worker_queues_[victim_worker].size() > steal_threshold_) {
            
            // Steal from the back (LIFO for better cache locality)
            auto stolen_task = std::move(worker_queues_[victim_worker].back());
            worker_queues_[victim_worker].pop_back();
            stolen_task.worker_id = requesting_worker_id;
            
            logger_->trace("Worker {} stole task from worker {}", 
                          requesting_worker_id, victim_worker);
            
            return stolen_task;
        }
    }
    
    return std::nullopt;
}

bool should_steal_work(int worker_id) const override {
    // Check if local queue is empty and other workers have work
    {
        std::shared_lock lock(worker_queue_mutexes_[worker_id]);
        if (!worker_queues_[worker_id].empty()) {
            return false; // Still have local work
        }
    }
    
    // Check if any other worker has enough work to steal
    for (int i = 0; i < static_cast<int>(worker_count_); ++i) {
        if (i != worker_id) {
            std::shared_lock lock(worker_queue_mutexes_[i]);
            if (worker_queues_[i].size() > steal_threshold_) {
                return true;
            }
        }
    }
    
    return false;
}

void rebalance_load() override {
    // Calculate total tasks and average per worker
    size_t total_tasks = 0;
    std::vector<size_t> queue_sizes(worker_count_);
    
    for (size_t i = 0; i < worker_count_; ++i) {
        std::shared_lock lock(worker_queue_mutexes_[i]);
        queue_sizes[i] = worker_queues_[i].size();
        total_tasks += queue_sizes[i];
    }
    
    if (total_tasks < worker_count_) {
        return; // Not enough work to balance
    }
    
    size_t average_tasks = total_tasks / worker_count_;
    size_t rebalance_threshold = average_tasks * 2; // 2x average
    
    // Move tasks from overloaded workers to underloaded ones
    for (size_t i = 0; i < worker_count_; ++i) {
        if (queue_sizes[i] > rebalance_threshold) {
            size_t excess = queue_sizes[i] - average_tasks;
            
            // Find underloaded workers
            for (size_t j = 0; j < worker_count_ && excess > 0; ++j) {
                if (i != j && queue_sizes[j] < average_tasks) {
                    size_t move_count = std::min(excess, average_tasks - queue_sizes[j]);
                    
                    // Move tasks
                    std::vector<SchedulableTask> tasks_to_move;
                    {
                        std::unique_lock lock(worker_queue_mutexes_[i]);
                        for (size_t k = 0; k < move_count && !worker_queues_[i].empty(); ++k) {
                            tasks_to_move.push_back(std::move(worker_queues_[i].back()));
                            worker_queues_[i].pop_back();
                        }
                    }
                    
                    {
                        std::unique_lock lock(worker_queue_mutexes_[j]);
                        for (auto& task : tasks_to_move) {
                            worker_queues_[j].push_back(std::move(task));
                        }
                    }
                    
                    excess -= tasks_to_move.size();
                    queue_sizes[j] += tasks_to_move.size();
                    
                    logger_->trace("Rebalanced {} tasks from worker {} to worker {}", 
                                  tasks_to_move.size(), i, j);
                }
            }
        }
    }
}

bool has_work_available() const override {
    for (size_t i = 0; i < worker_count_; ++i) {
        std::shared_lock lock(worker_queue_mutexes_[i]);
        if (!worker_queues_[i].empty()) {
            return true;
        }
    }
    return false;
}

std::vector<size_t> get_queue_depths() const override {
    std::vector<size_t> depths;
    depths.reserve(worker_count_);
    
    for (size_t i = 0; i < worker_count_; ++i) {
        std::shared_lock lock(worker_queue_mutexes_[i]);
        depths.push_back(worker_queues_[i].size());
    }
    
    return depths;
}

std::string get_scheduler_info() const override {
    std::ostringstream oss;
    oss << "WorkStealingScheduler:\n";
    oss << "  Strategy: Work-stealing with LIFO local deques\n";
    oss << "  Workers: " << worker_count_ << "\n";
    oss << "  Running: " << (running_.load() ? "yes" : "no") << "\n";
    oss << "  Steal Threshold: " << steal_threshold_ << " tasks\n";
    oss << "  Steal Interval: " << steal_interval_.count() << "μs\n";
    
    auto depths = get_queue_depths();
    oss << "  Queue Depths: [";
    for (size_t i = 0; i < depths.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << depths[i];
    }
    oss << "]\n";
    
    return oss.str();
}
```

private:
int choose_target_worker() const {
// Choose worker with minimum queue size
int best_worker = 0;
size_t min_size = SIZE_MAX;

```
    for (size_t i = 0; i < worker_count_; ++i) {
        std::shared_lock lock(worker_queue_mutexes_[i]);
        if (worker_queues_[i].size() < min_size) {
            min_size = worker_queues_[i].size();
            best_worker = static_cast<int>(i);
        }
    }
    
    return best_worker;
}
```

};

// ============================================================================
// Round-Robin Scheduler Implementation
// ============================================================================

class RoundRobinScheduler : public BaseTaskScheduler {
private:
std::vector<std::queue<SchedulableTask>> worker_queues_;
mutable std::vector<std::mutex> worker_queue_mutexes_;
std::atomic<size_t> next_worker_{0};

public:
explicit RoundRobinScheduler(const RuntimeConfig& config)
: BaseTaskScheduler(config)
, worker_queues_(config.worker.worker_threads)
, worker_queue_mutexes_(config.worker.worker_threads) {}

protected:
void schedule_task_impl(SchedulableTask task) override {
// Round-robin assignment
int target_worker = next_worker_.fetch_add(1, std::memory_order_relaxed) % worker_count_;

```
    {
        std::unique_lock lock(worker_queue_mutexes_[target_worker]);
        worker_queues_[target_worker].push(std::move(task));
    }
    
    queue_cv_.notify_one();
}

void schedule_batch_impl(std::vector<SchedulableTask> tasks) override {
    for (size_t i = 0; i < tasks.size(); ++i) {
        int target_worker = (next_worker_.load() + i) % worker_count_;
        
        {
            std::unique_lock lock(worker_queue_mutexes_[target_worker]);
            worker_queues_[target_worker].push(std::move(tasks[i]));
        }
    }
    
    next_worker_.fetch_add(tasks.size(), std::memory_order_relaxed);
    queue_cv_.notify_all();
}

std::optional<SchedulableTask> get_next_task(int worker_id) override {
    std::unique_lock lock(worker_queue_mutexes_[worker_id]);
    
    if (worker_queues_[worker_id].empty()) {
        return std::nullopt;
    }
    
    auto task = std::move(worker_queues_[worker_id].front());
    worker_queues_[worker_id].pop();
    task.worker_id = worker_id;
    
    return task;
}

bool has_work_available() const override {
    for (size_t i = 0; i < worker_count_; ++i) {
        std::unique_lock lock(worker_queue_mutexes_[i]);
        if (!worker_queues_[i].empty()) {
            return true;
        }
    }
    return false;
}

std::string get_scheduler_info() const override {
    std::ostringstream oss;
    oss << "RoundRobinScheduler:\n";
    oss << "  Strategy: Round-robin task distribution\n";
    oss << "  Workers: " << worker_count_ << "\n";
    oss << "  Running: " << (running_.load() ? "yes" : "no") << "\n";
    oss << "  Next Worker: " << (next_worker_.load() % worker_count_) << "\n";
    
    return oss.str();
}
```

};

// ============================================================================
// Affinity Scheduler Implementation
// ============================================================================

class AffinityScheduler : public BaseTaskScheduler {
private:
// Affinity group to worker mapping
std::unordered_map<std::string, std::vector<int>> affinity_groups_;

```
// Per-worker queues
std::vector<std::queue<SchedulableTask>> worker_queues_;
mutable std::vector<std::mutex> worker_queue_mutexes_;

// Fallback queue for tasks without affinity
std::queue<SchedulableTask> fallback_queue_;
mutable std::mutex fallback_queue_mutex_;
```

public:
explicit AffinityScheduler(const RuntimeConfig& config)
: BaseTaskScheduler(config)
, worker_queues_(config.worker.worker_threads)
, worker_queue_mutexes_(config.worker.worker_threads) {

```
    initialize_affinity_groups();
}
```

private:
void initialize_affinity_groups() {
// Create default affinity groups based on NUMA topology
if (config_.performance.numa_aware) {
auto topology = get_numa_topology();

```
        for (const auto& [numa_node, cpus] : topology) {
            std::string group_name = "numa_" + std::to_string(numa_node);
            std::vector<int> workers;
            
            // Assign workers to this NUMA node
            for (size_t i = 0; i < worker_count_; ++i) {
                if (workers_[i].numa_node == numa_node) {
                    workers.push_back(static_cast<int>(i));
                }
            }
            
            if (!workers.empty()) {
                affinity_groups_[group_name] = std::move(workers);
            }
        }
    }
    
    // Create CPU-based affinity groups
    std::unordered_map<int, std::vector<int>> cpu_groups;
    for (size_t i = 0; i < worker_count_; ++i) {
        if (workers_[i].cpu_core >= 0) {
            cpu_groups[workers_[i].cpu_core].push_back(static_cast<int>(i));
        }
    }
    
    for (const auto& [cpu_core, workers] : cpu_groups) {
        std::string group_name = "cpu_" + std::to_string(cpu_core);
        affinity_groups_[group_name] = workers;
    }
    
    // Default group (all workers)
    std::vector<int> all_workers;
    for (int i = 0; i < static_cast<int>(worker_count_); ++i) {
        all_workers.push_back(i);
    }
    affinity_groups_["default"] = std::move(all_workers);
    
    logger_->info("Initialized {} affinity groups", affinity_groups_.size());
}
```

protected:
void schedule_task_impl(SchedulableTask task) override {
int target_worker = choose_affinity_worker(task.affinity);

```
    if (target_worker >= 0) {
        std::unique_lock lock(worker_queue_mutexes_[target_worker]);
        worker_queues_[target_worker].push(std::move(task));
    } else {
        // No suitable worker found, use fallback queue
        std::unique_lock lock(fallback_queue_mutex_);
        fallback_queue_.push(std::move(task));
    }
    
    queue_cv_.notify_one();
}

void schedule_batch_impl(std::vector<SchedulableTask> tasks) override {
    for (auto& task : tasks) {
        schedule_task_impl(std::move(task));
    }
}

std::optional<SchedulableTask> get_next_task(int worker_id) override {
    // First, try worker-specific queue
    {
        std::unique_lock lock(worker_queue_mutexes_[worker_id]);
        if (!worker_queues_[worker_id].empty()) {
            auto task = std::move(worker_queues_[worker_id].front());
            worker_queues_[worker_id].pop();
            task.worker_id = worker_id;
            return task;
        }
    }
    
    // Try fallback queue if worker is suitable
    {
        std::unique_lock lock(fallback_queue_mutex_);
        if (!fallback_queue_.empty()) {
            auto task = std::move(fallback_queue_.front());
            fallback_queue_.pop();
            task.worker_id = worker_id;
            return task;
        }
    }
    
    return std::nullopt;
}

bool has_work_available() const override {
    // Check worker queues
    for (size_t i = 0; i < worker_count_; ++i) {
        std::unique_lock lock(worker_queue_mutexes_[i]);
        if (!worker_queues_[i].empty()) {
            return true;
        }
    }
    
    // Check fallback queue
    std::unique_lock lock(fallback_queue_mutex_);
    return !fallback_queue_.empty();
}

std::string get_scheduler_info() const override {
    std::ostringstream oss;
    oss << "AffinityScheduler:\n";
    oss << "  Strategy: Affinity-based task placement\n";
    oss << "  Workers: " << worker_count_ << "\n";
    oss << "  Running: " << (running_.load() ? "yes" : "no") << "\n";
    oss << "  Affinity Groups: " << affinity_groups_.size() << "\n";
    
    for (const auto& [group_name, workers] : affinity_groups_) {
        oss << "    " << group_name << ": [";
        for (size_t i = 0; i < workers.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << workers[i];
        }
        oss << "]\n";
    }
    
    return oss.str();
}
```

private:
int choose_affinity_worker(const TaskAffinity& affinity) {
// Preferred worker has highest priority
if (affinity.preferred_worker &&
*affinity.preferred_worker >= 0 &&
*affinity.preferred_worker < static_cast<int>(worker_count_)) {
return *affinity.preferred_worker;
}

```
    // Try affinity group
    if (!affinity.affinity_group.empty()) {
        auto it = affinity_groups_.find(affinity.affinity_group);
        if (it != affinity_groups_.end() && !it->second.empty()) {
            // Choose worker with least load in the group
            int best_worker = it->second[0];
            size_t min_load = get_worker_queue_size(best_worker);
            
            for (int worker : it->second) {
                size_t load = get_worker_queue_size(worker);
                if (load < min_load) {
                    min_load = load;
                    best_worker = worker;
                }
            }
            
            return best_worker;
        }
    }
    
    // Try NUMA node affinity
    if (affinity.numa_node) {
        std::string numa_group = "numa_" + std::to_string(*affinity.numa_node);
        auto it = affinity_groups_.find(numa_group);
        if (it != affinity_groups_.end() && !it->second.empty()) {
            return it->second[0]; // Simple choice
        }
    }
    
    return -1; // No suitable worker found
}

size_t get_worker_queue_size(int worker_id) const {
    if (worker_id < 0 || worker_id >= static_cast<int>(worker_count_)) {
        return SIZE_MAX;
    }
    
    std::unique_lock lock(worker_queue_mutexes_[worker_id]);
    return worker_queues_[worker_id].size();
}
```

};

// ============================================================================
// Task Scheduler Factory
// ============================================================================

class TaskSchedulerFactory {
public:
static std::unique_ptr<ITaskScheduler> create(const RuntimeConfig& config) {
switch (config.strategy) {
case TaskSchedulingStrategy::FIFO:
return std::make_unique<FIFOScheduler>(config);

```
        case TaskSchedulingStrategy::PRIORITY:
            return std::make_unique<PriorityScheduler>(config);
            
        case TaskSchedulingStrategy::WORK_STEALING:
            return std::make_unique<WorkStealingScheduler>(config);
            
        case TaskSchedulingStrategy::ROUND_ROBIN:
            return std::make_unique<RoundRobinScheduler>(config);
            
        case TaskSchedulingStrategy::AFFINITY:
            return std::make_unique<AffinityScheduler>(config);
            
        case TaskSchedulingStrategy::CUSTOM:
            if (config.custom_scheduler_factory) {
                return config.custom_scheduler_factory();
            } else {
                throw SchedulerInitializationException("custom", "No custom factory provided");
            }
            
        default:
            throw SchedulerInitializationException("unknown", 
                "Unknown scheduling strategy: " + std::to_string(static_cast<int>(config.strategy)));
    }
}

static std::vector<TaskSchedulingStrategy> get_available_strategies() {
    return {
        TaskSchedulingStrategy::FIFO,
        TaskSchedulingStrategy::PRIORITY,
        TaskSchedulingStrategy::WORK_STEALING,
        TaskSchedulingStrategy::ROUND_ROBIN,
        TaskSchedulingStrategy::AFFINITY,
        TaskSchedulingStrategy::CUSTOM
    };
}

static TaskSchedulingStrategy get_recommended_strategy(const RuntimeConfig& config) {
    size_t worker_count = config.worker.worker_threads;
    
    if (worker_count == 1) {
        return TaskSchedulingStrategy::FIFO;
    } else if (worker_count <= 4) {
        return TaskSchedulingStrategy::ROUND_ROBIN;
    } else {
        return TaskSchedulingStrategy::WORK_STEALING;
    }
}
```

};

// ============================================================================
// Global Factory Function
// ============================================================================

std::unique_ptr<ITaskScheduler> create_task_scheduler(const RuntimeConfig& config) {
return TaskSchedulerFactory::create(config);
}

} // namespace stellane
