#include “stellane/runtime/runtime_stats.h”
#include “stellane/runtime/runtime_config.h”
#include “stellane/utils/logger.h”

#include <sstream>
#include <algorithm>
#include <numeric>
#include <iomanip>
#include <cstring>
#include <cmath>

#ifdef **linux**
#include <sys/resource.h>
#include <unistd.h>
#include <fstream>
#include <numa.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#elif defined(**APPLE**)
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace stellane::runtime {

// ============================================================================
// Internal Utilities for System Metrics
// ============================================================================

namespace {

/**

- @brief Cross-platform CPU usage calculation
  */
  double get_cpu_usage_percent() {
  #ifdef **linux**
  static uint64_t last_total = 0, last_idle = 0;
  
  std::ifstream stat_file(”/proc/stat”);
  if (!stat_file.is_open()) return 0.0;
  
  std::string cpu_label;
  uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
  
  stat_file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
  
  uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
  uint64_t idle_time = idle + iowait;
  
  double cpu_usage = 0.0;
  if (last_total != 0) {
  uint64_t total_diff = total - last_total;
  uint64_t idle_diff = idle_time - last_idle;
  cpu_usage = total_diff > 0 ? (100.0 * (total_diff - idle_diff) / total_diff) : 0.0;
  }
  
  last_total = total;
  last_idle = idle_time;
  
  return cpu_usage;

#elif defined(_WIN32)
static ULARGE_INTEGER last_kernel, last_user, last_idle;
static bool first_call = true;

```
FILETIME idle_time, kernel_time, user_time;
if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
    return 0.0;
}

ULARGE_INTEGER curr_idle, curr_kernel, curr_user;
curr_idle.LowPart = idle_time.dwLowDateTime;
curr_idle.HighPart = idle_time.dwHighDateTime;
curr_kernel.LowPart = kernel_time.dwLowDateTime;
curr_kernel.HighPart = kernel_time.dwHighDateTime;
curr_user.LowPart = user_time.dwLowDateTime;
curr_user.HighPart = user_time.dwHighDateTime;

if (first_call) {
    first_call = false;
    last_idle = curr_idle;
    last_kernel = curr_kernel;
    last_user = curr_user;
    return 0.0;
}

uint64_t idle_diff = curr_idle.QuadPart - last_idle.QuadPart;
uint64_t kernel_diff = curr_kernel.QuadPart - last_kernel.QuadPart;
uint64_t user_diff = curr_user.QuadPart - last_user.QuadPart;
uint64_t total_diff = kernel_diff + user_diff;

double cpu_usage = total_diff > 0 ? (100.0 * (total_diff - idle_diff) / total_diff) : 0.0;

last_idle = curr_idle;
last_kernel = curr_kernel;
last_user = curr_user;

return cpu_usage;
```

#else
// Fallback for other platforms
return 0.0;
#endif
}

/**

- @brief Get current memory usage in bytes
  */
  size_t get_memory_usage_bytes() {
  #ifdef **linux**
  std::ifstream status_file(”/proc/self/status”);
  if (!status_file.is_open()) return 0;
  
  std::string line;
  while (std::getline(status_file, line)) {
  if (line.substr(0, 6) == “VmRSS:”) {
  std::istringstream iss(line);
  std::string label, size_str, unit;
  iss >> label >> size_str >> unit;
  
  ```
       size_t size = std::stoull(size_str);
       if (unit == "kB") {
           return size * 1024;
       }
       break;
   }
  ```
  
  }
  return 0;

#elif defined(_WIN32)
PROCESS_MEMORY_COUNTERS pmc;
if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
return pmc.WorkingSetSize;
}
return 0;

#elif defined(**APPLE**)
struct mach_task_basic_info info;
mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;

```
if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
              (task_info_t)&info, &infoCount) == KERN_SUCCESS) {
    return info.resident_size;
}
return 0;
```

#else
return 0;
#endif
}

/**

- @brief Get stack usage for current thread
  */
  size_t get_stack_usage_bytes() {
  #ifdef **linux**
  // Linux-specific stack usage estimation
  void* stack_ptr;
  asm volatile (“movq %%rsp, %0” : “=r” (stack_ptr));
  
  pthread_attr_t attr;
  pthread_getattr_np(pthread_self(), &attr);
  
  void* stack_base;
  size_t stack_size;
  pthread_attr_getstack(&attr, &stack_base, &stack_size);
  pthread_attr_destroy(&attr);
  
  if (stack_ptr >= stack_base && stack_ptr < (char*)stack_base + stack_size) {
  return (char*)stack_base + stack_size - (char*)stack_ptr;
  }

#endif
return 8192; // Default estimate: 8KB
}

/**

- @brief Get number of open file descriptors
  */
  size_t get_open_file_descriptors() {
  #ifdef **linux**
  size_t count = 0;
  DIR* fd_dir = opendir(”/proc/self/fd”);
  if (fd_dir) {
  struct dirent* entry;
  while ((entry = readdir(fd_dir)) != nullptr) {
  if (entry->d_name[0] != ‘.’) {
  count++;
  }
  }
  closedir(fd_dir);
  }
  return count;
  #else
  return 0; // Not implemented for other platforms
  #endif
  }

/**

- @brief Calculate moving average with exponential decay
  */
  double update_moving_average(double current_avg, double new_value, double alpha = 0.1) {
  return alpha * new_value + (1.0 - alpha) * current_avg;
  }

/**

- @brief Calculate percentile from sorted values
  */
  template<typename Container>
  double calculate_percentile(const Container& values, double percentile) {
  if (values.empty()) return 0.0;
  
  auto sorted_values = values;
  std::sort(sorted_values.begin(), sorted_values.end());
  
  double index = (percentile / 100.0) * (sorted_values.size() - 1);
  size_t lower_index = static_cast<size_t>(std::floor(index));
  size_t upper_index = static_cast<size_t>(std::ceil(index));
  
  if (lower_index == upper_index) {
  return sorted_values[lower_index];
  }
  
  double weight = index - lower_index;
  return sorted_values[lower_index] * (1.0 - weight) + sorted_values[upper_index] * weight;
  }

} // anonymous namespace

// ============================================================================
// RuntimeStats Implementation
// ============================================================================

std::string RuntimeStats::to_string() const {
std::ostringstream oss;

```
oss << "=== Stellane Runtime Statistics ===\n";
oss << "Uptime: " << uptime.count() << "ms\n";
oss << "Backend: " << backend_type << " (" << worker_count << " workers)\n";
oss << "Scheduling: " << scheduling_strategy << "\n\n";

oss << "Task Execution:\n";
oss << "  Total Executed: " << total_tasks_executed.load() << "\n";
oss << "  Total Failed: " << total_tasks_failed.load() << "\n";
oss << "  Success Rate: " << std::fixed << std::setprecision(2) << success_rate() << "%\n";
oss << "  Currently Active: " << current_active_tasks.load() << "\n";
oss << "  Currently Pending: " << current_pending_tasks.load() << "\n";
oss << "  Peak Active: " << peak_active_tasks.load() << "\n";
oss << "  Peak Pending: " << peak_pending_tasks.load() << "\n\n";

oss << "Performance:\n";
oss << "  Tasks/Second: " << tasks_per_second.load() << "\n";
oss << "  Peak Tasks/Second: " << peak_tasks_per_second.load() << "\n";
oss << "  Avg Duration: " << average_task_duration_ms.load() << "ms\n";
oss << "  P95 Duration: " << p95_task_duration_ms.load() << "ms\n";
oss << "  P99 Duration: " << p99_task_duration_ms.load() << "ms\n";
oss << "  Load Factor: " << std::fixed << std::setprecision(2) << load_factor() << "\n\n";

oss << "Memory Usage:\n";
oss << "  Current Heap: " << (heap_usage_bytes.load() / (1024 * 1024)) << "MB\n";
oss << "  Peak Heap: " << (peak_heap_usage_bytes.load() / (1024 * 1024)) << "MB\n";
oss << "  Task Pool: " << (task_pool_usage_bytes.load() / (1024 * 1024)) << "MB\n";
oss << "  Stack Usage: " << (stack_usage_bytes.load() / 1024) << "KB\n\n";

if (total_recovery_attempts.load() > 0) {
    oss << "Recovery System:\n";
    oss << "  Total Attempts: " << total_recovery_attempts.load() << "\n";
    oss << "  Successful: " << successful_recoveries.load() << "\n";
    oss << "  Failed: " << failed_recoveries.load() << "\n";
    oss << "  Success Rate: " << std::fixed << std::setprecision(2) << recovery_success_rate() << "%\n";
    oss << "  Avg Recovery Time: " << average_recovery_time_ms.load() << "ms\n\n";
}

return oss.str();
```

}

// ============================================================================
// Default StatsCollector Implementation
// ============================================================================

class DefaultStatsCollector : public IStatsCollector {
private:
// Core statistics storage
RuntimeStats runtime_stats_;
std::vector<WorkerStats> worker_stats_;
BackendStats backend_stats_;
std::optional<ProfilingStats> profiling_stats_;

```
// Configuration
bool profiling_enabled_ = false;
std::chrono::steady_clock::time_point last_update_;

// Thread safety
mutable std::shared_mutex stats_mutex_;

// Profiling data storage (when enabled)
std::vector<double> recent_task_durations_;
std::atomic<size_t> profiling_sample_count_{0};
static constexpr size_t MAX_PROFILING_SAMPLES = 10000;

// Update intervals
static constexpr auto STATS_UPDATE_INTERVAL = std::chrono::milliseconds(100);
static constexpr auto CPU_SAMPLE_INTERVAL = std::chrono::seconds(1);
```

public:
DefaultStatsCollector(size_t worker_count, const std::string& backend_type) {
// Initialize runtime stats
runtime_stats_.start_time = std::chrono::steady_clock::now();
runtime_stats_.last_update = runtime_stats_.start_time;
runtime_stats_.backend_type = backend_type;
runtime_stats_.worker_count = static_cast<int>(worker_count);

```
    // Initialize worker stats
    worker_stats_.resize(worker_count);
    for (size_t i = 0; i < worker_count; ++i) {
        worker_stats_[i].worker_id = static_cast<int>(i);
        worker_stats_[i].last_activity = std::chrono::steady_clock::now();
    }
    
    last_update_ = std::chrono::steady_clock::now();
}

// ========================================================================
// IStatsCollector Interface Implementation
// ========================================================================

RuntimeStats get_runtime_stats() const override {
    std::shared_lock lock(stats_mutex_);
    
    // Update uptime
    auto current_time = std::chrono::steady_clock::now();
    runtime_stats_.uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
        current_time - runtime_stats_.start_time);
    runtime_stats_.last_update = current_time;
    
    return runtime_stats_;
}

std::vector<WorkerStats> get_worker_stats() const override {
    std::shared_lock lock(stats_mutex_);
    return worker_stats_;
}

BackendStats get_backend_stats() const override {
    std::shared_lock lock(stats_mutex_);
    return backend_stats_;
}

std::optional<ProfilingStats> get_profiling_stats() const override {
    std::shared_lock lock(stats_mutex_);
    return profiling_stats_;
}

void reset_stats() override {
    std::unique_lock lock(stats_mutex_);
    
    // Reset runtime stats (but preserve start time and configuration)
    auto start_time = runtime_stats_.start_time;
    auto backend_type = runtime_stats_.backend_type;
    auto worker_count = runtime_stats_.worker_count;
    auto scheduling_strategy = runtime_stats_.scheduling_strategy;
    
    runtime_stats_ = RuntimeStats{};
    runtime_stats_.start_time = start_time;
    runtime_stats_.backend_type = backend_type;
    runtime_stats_.worker_count = worker_count;
    runtime_stats_.scheduling_strategy = scheduling_strategy;
    
    // Reset worker stats
    for (auto& worker : worker_stats_) {
        int worker_id = worker.worker_id;
        auto thread_id = worker.thread_id;
        int cpu_core = worker.cpu_core;
        int numa_node = worker.numa_node;
        
        worker = WorkerStats{};
        worker.worker_id = worker_id;
        worker.thread_id = thread_id;
        worker.cpu_core = cpu_core;
        worker.numa_node = numa_node;
        worker.last_activity = std::chrono::steady_clock::now();
    }
    
    // Reset backend stats
    backend_stats_ = BackendStats{};
    
    // Reset profiling stats
    if (profiling_enabled_) {
        profiling_stats_ = ProfilingStats{};
        recent_task_durations_.clear();
        profiling_sample_count_ = 0;
    }
}

void enable_profiling(bool enabled) override {
    std::unique_lock lock(stats_mutex_);
    
    profiling_enabled_ = enabled;
    if (enabled && !profiling_stats_) {
        profiling_stats_ = ProfilingStats{};
        recent_task_durations_.clear();
        recent_task_durations_.reserve(MAX_PROFILING_SAMPLES);
    } else if (!enabled) {
        profiling_stats_.reset();
        recent_task_durations_.clear();
        recent_task_durations_.shrink_to_fit();
    }
}

void update_stats() override {
    auto now = std::chrono::steady_clock::now();
    
    // Skip update if too frequent
    if (now - last_update_ < STATS_UPDATE_INTERVAL) {
        return;
    }
    
    std::unique_lock lock(stats_mutex_);
    
    // Update system-level metrics
    update_system_metrics();
    
    // Update task performance metrics
    update_task_metrics();
    
    // Update worker-specific metrics
    update_worker_metrics();
    
    // Update backend metrics
    update_backend_metrics();
    
    // Update profiling metrics (if enabled)
    if (profiling_enabled_) {
        update_profiling_metrics();
    }
    
    last_update_ = now;
}

void record_task_execution(int worker_id, double duration_ms, bool success) override {
    // Update atomic counters (lock-free)
    if (success) {
        runtime_stats_.total_tasks_executed.fetch_add(1, std::memory_order_relaxed);
    } else {
        runtime_stats_.total_tasks_failed.fetch_add(1, std::memory_order_relaxed);
    }
    
    // Update moving averages
    double current_avg = runtime_stats_.average_task_duration_ms.load(std::memory_order_relaxed);
    double new_avg = update_moving_average(current_avg, duration_ms);
    runtime_stats_.average_task_duration_ms.store(new_avg, std::memory_order_relaxed);
    
    // Update worker-specific stats
    if (worker_id >= 0 && worker_id < static_cast<int>(worker_stats_.size())) {
        auto& worker = worker_stats_[worker_id];
        
        if (success) {
            worker.tasks_executed.fetch_add(1, std::memory_order_relaxed);
        } else {
            worker.tasks_failed.fetch_add(1, std::memory_order_relaxed);
        }
        
        // Update worker's moving average
        double worker_avg = worker.average_task_duration_ms.load(std::memory_order_relaxed);
        double new_worker_avg = update_moving_average(worker_avg, duration_ms);
        worker.average_task_duration_ms.store(new_worker_avg, std::memory_order_relaxed);
        
        worker.last_activity = std::chrono::steady_clock::now();
    }
    
    // Add to profiling data (if enabled)
    if (profiling_enabled_ && profiling_sample_count_.load() < MAX_PROFILING_SAMPLES) {
        std::unique_lock lock(stats_mutex_);
        if (recent_task_durations_.size() < MAX_PROFILING_SAMPLES) {
            recent_task_durations_.push_back(duration_ms);
            profiling_sample_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void record_io_event(const std::string& event_type, size_t bytes_processed) override {
    backend_stats_.total_io_events.fetch_add(1, std::memory_order_relaxed);
    
    if (event_type == "read") {
        backend_stats_.read_events.fetch_add(1, std::memory_order_relaxed);
        backend_stats_.bytes_read.fetch_add(bytes_processed, std::memory_order_relaxed);
    } else if (event_type == "write") {
        backend_stats_.write_events.fetch_add(1, std::memory_order_relaxed);
        backend_stats_.bytes_written.fetch_add(bytes_processed, std::memory_order_relaxed);
    } else if (event_type == "error") {
        backend_stats_.error_events.fetch_add(1, std::memory_order_relaxed);
    } else if (event_type == "timeout") {
        backend_stats_.timeout_events.fetch_add(1, std::memory_order_relaxed);
    }
}

void record_allocation(size_t size_bytes) override {
    runtime_stats_.heap_usage_bytes.fetch_add(size_bytes, std::memory_order_relaxed);
    
    // Update peak if necessary
    size_t current_heap = runtime_stats_.heap_usage_bytes.load(std::memory_order_relaxed);
    size_t current_peak = runtime_stats_.peak_heap_usage_bytes.load(std::memory_order_relaxed);
    while (current_heap > current_peak) {
        if (runtime_stats_.peak_heap_usage_bytes.compare_exchange_weak(
            current_peak, current_heap, std::memory_order_relaxed)) {
            break;
        }
    }
    
    if (profiling_enabled_ && profiling_stats_) {
        profiling_stats_->total_allocations.fetch_add(1, std::memory_order_relaxed);
        
        // Update average allocation size
        double current_avg = profiling_stats_->average_allocation_size.load(std::memory_order_relaxed);
        double new_avg = update_moving_average(current_avg, static_cast<double>(size_bytes));
        profiling_stats_->average_allocation_size.store(new_avg, std::memory_order_relaxed);
        
        // Update peak allocation size
        size_t current_peak_alloc = profiling_stats_->peak_allocation_size.load(std::memory_order_relaxed);
        while (size_bytes > current_peak_alloc) {
            if (profiling_stats_->peak_allocation_size.compare_exchange_weak(
                current_peak_alloc, size_bytes, std::memory_order_relaxed)) {
                break;
            }
        }
    }
}

void record_deallocation(size_t size_bytes) override {
    // Prevent underflow
    size_t current_heap = runtime_stats_.heap_usage_bytes.load(std::memory_order_relaxed);
    if (current_heap >= size_bytes) {
        runtime_stats_.heap_usage_bytes.fetch_sub(size_bytes, std::memory_order_relaxed);
    }
    
    if (profiling_enabled_ && profiling_stats_) {
        profiling_stats_->total_deallocations.fetch_add(1, std::memory_order_relaxed);
    }
}

// ========================================================================
// Internal Update Methods
// ========================================================================
```

private:
void update_system_metrics() {
// Update memory usage from system
size_t system_memory = get_memory_usage_bytes();
runtime_stats_.heap_usage_bytes.store(system_memory, std::memory_order_relaxed);

```
    // Update stack usage estimate
    size_t stack_usage = get_stack_usage_bytes() * worker_stats_.size();
    runtime_stats_.stack_usage_bytes.store(stack_usage, std::memory_order_relaxed);
}

void update_task_metrics() {
    // Calculate tasks per second
    auto total_tasks = runtime_stats_.total_tasks_executed.load(std::memory_order_relaxed);
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - runtime_stats_.start_time).count();
    
    if (uptime_seconds > 0) {
        size_t current_tps = total_tasks / uptime_seconds;
        runtime_stats_.tasks_per_second.store(current_tps, std::memory_order_relaxed);
        
        // Update peak TPS
        size_t peak_tps = runtime_stats_.peak_tasks_per_second.load(std::memory_order_relaxed);
        while (current_tps > peak_tps) {
            if (runtime_stats_.peak_tasks_per_second.compare_exchange_weak(
                peak_tps, current_tps, std::memory_order_relaxed)) {
                break;
            }
        }
    }
    
    // Update percentile calculations (if profiling enabled)
    if (profiling_enabled_ && !recent_task_durations_.empty()) {
        runtime_stats_.p95_task_duration_ms.store(
            calculate_percentile(recent_task_durations_, 95.0), std::memory_order_relaxed);
        runtime_stats_.p99_task_duration_ms.store(
            calculate_percentile(recent_task_durations_, 99.0), std::memory_order_relaxed);
    }
}

void update_worker_metrics() {
    for (auto& worker : worker_stats_) {
        // Update CPU usage (simplified estimation)
        double cpu_usage = get_cpu_usage_percent() / worker_stats_.size(); // Distribute among workers
        worker.cpu_usage_percent.store(cpu_usage, std::memory_order_relaxed);
        
        // Calculate worker tasks per second
        auto worker_tasks = worker.tasks_executed.load(std::memory_order_relaxed);
        auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - runtime_stats_.start_time).count();
        
        if (uptime_seconds > 0) {
            size_t worker_tps = worker_tasks / uptime_seconds;
            worker.tasks_per_second.store(worker_tps, std::memory_order_relaxed);
        }
    }
}

void update_backend_metrics() {
    // Calculate I/O throughput
    auto total_bytes_read = backend_stats_.bytes_read.load(std::memory_order_relaxed);
    auto total_bytes_written = backend_stats_.bytes_written.load(std::memory_order_relaxed);
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - runtime_stats_.start_time).count();
    
    if (uptime_seconds > 0) {
        double read_mbps = (total_bytes_read / (1024.0 * 1024.0)) / uptime_seconds;
        double write_mbps = (total_bytes_written / (1024.0 * 1024.0)) / uptime_seconds;
        
        backend_stats_.read_throughput_mbps.store(read_mbps, std::memory_order_relaxed);
        backend_stats_.write_throughput_mbps.store(write_mbps, std::memory_order_relaxed);
    }
    
    // Calculate events per poll (if applicable)
    auto total_polls = backend_stats_.poll_calls.load(std::memory_order_relaxed);
    auto total_events = backend_stats_.total_io_events.load(std::memory_order_relaxed);
    
    if (total_polls > 0) {
        size_t events_per_poll = total_events / total_polls;
        backend_stats_.events_per_poll.store(events_per_poll, std::memory_order_relaxed);
    }
}

void update_profiling_metrics() {
    if (!profiling_stats_) return;
    
    // Update system resource usage
    profiling_stats_->system_cpu_usage.store(get_cpu_usage_percent(), std::memory_order_relaxed);
    profiling_stats_->system_memory_usage.store(get_memory_usage_bytes(), std::memory_order_relaxed);
    profiling_stats_->file_descriptors_used.store(get_open_file_descriptors(), std::memory_order_relaxed);
}
```

};

// ============================================================================
// Statistics Export and Formatting
// ============================================================================

namespace stats_formatter {

std::string to_json(const RuntimeStats& stats) {
std::ostringstream oss;
oss << “{\n”;
oss << “  "uptime_ms": “ << stats.uptime.count() << “,\n”;
oss << “  "backend_type": "” << stats.backend_type << “",\n”;
oss << “  "worker_count": “ << stats.worker_count << “,\n”;
oss << “  "scheduling_strategy": "” << stats.scheduling_strategy << “",\n”;
oss << “  "profiling_enabled": “ << (stats.profiling_enabled ? “true” : “false”) << “,\n”;
oss << “  "tasks": {\n”;
oss << “    "total_executed": “ << stats.total_tasks_executed.load() << “,\n”;
oss << “    "total_failed": “ << stats.total_tasks_failed.load() << “,\n”;
oss << “    "current_active": “ << stats.current_active_tasks.load() << “,\n”;
oss << “    "current_pending": “ << stats.current_pending_tasks.load() << “,\n”;
oss << “    "peak_active": “ << stats.peak_active_tasks.load() << “,\n”;
oss << “    "peak_pending": “ << stats.peak_pending_tasks.load() << “,\n”;
oss << “    "success_rate": “ << std::fixed << std::setprecision(2) << stats.success_rate() << “\n”;
oss << “  },\n”;
oss << “  "performance": {\n”;
oss << “    "tasks_per_second": “ << stats.tasks_per_second.load() << “,\n”;
oss << “    "peak_tasks_per_second": “ << stats.peak_tasks_per_second.load() << “,\n”;
oss << “    "average_duration_ms": “ << std::fixed << std::setprecision(3) << stats.average_task_duration_ms.load() << “,\n”;
oss << “    "p95_duration_ms": “ << std::fixed << std::setprecision(3) << stats.p95_task_duration_ms.load() << “,\n”;
oss << “    "p99_duration_ms": “ << std::fixed << std::setprecision(3) << stats.p99_task_duration_ms.load() << “,\n”;
oss << “    "load_factor": “ << std::fixed << std::setprecision(3) << stats.load_factor() << “\n”;
oss << “  },\n”;
oss << “  "memory": {\n”;
oss << “    "heap_usage_bytes": “ << stats.heap_usage_bytes.load() << “,\n”;
oss << “    "peak_heap_usage_bytes": “ << stats.peak_heap_usage_bytes.load() << “,\n”;
oss << “    "task_pool_usage_bytes": “ << stats.task_pool_usage_bytes.load() << “,\n”;
oss << “    "stack_usage_bytes": “ << stats.stack_usage_bytes.load() << “\n”;
oss << “  }”;

```
// Add recovery stats if applicable
if (stats.total_recovery_attempts.load() > 0) {
    oss << ",\n  \"recovery\": {\n";
    oss << "    \"total_attempts\": " << stats.total_recovery_attempts.load() << ",\n";
    oss << "    \"successful_recoveries\": " << stats.successful_recoveries.load() << ",\n";
    oss << "    \"failed_recoveries\": " << stats.failed_recoveries.load() << ",\n";
    oss << "    \"success_rate\": " << std::fixed << std::setprecision(2) << stats.recovery_success_rate() << ",\n";
    oss << "    \"average_recovery_time_ms\": " << std::fixed << std::setprecision(3) << stats.average_recovery_time_ms.load() << "\n";
    oss << "  }";
}

oss << "\n}";
return oss.str();
```

}

std::string to_json(const std::vector<WorkerStats>& worker_stats) {
std::ostringstream oss;
oss << “{\n  "workers": [\n”;

```
for (size_t i = 0; i < worker_stats.size(); ++i) {
    const auto& worker = worker_stats[i];
    
    oss << "    {\n";
    oss << "      \"worker_id\": " << worker.worker_id << ",\n";
    oss << "      \"cpu_core\": " << worker.cpu_core << ",\n";
    oss << "      \"numa_node\": " << worker.numa_node << ",\n";
    oss << "      \"tasks\": {\n";
    oss << "        \"executed\": " << worker.tasks_executed.load() << ",\n";
    oss << "        \"failed\": " << worker.tasks_failed.load() << ",\n";
    oss << "        \"current_queue_size\": " << worker.current_queue_size.load() << ",\n";
    oss << "        \"peak_queue_size\": " << worker.peak_queue_size.load() << "\n";
    oss << "      },\n";
    oss << "      \"performance\": {\n";
    oss << "        \"cpu_usage_percent\": " << std::fixed << std::setprecision(2) << worker.cpu_usage_percent.load() << ",\n";
    oss << "        \"average_task_duration_ms\": " << std::fixed << std::setprecision(3) << worker.average_task_duration_ms.load() << ",\n";
    oss << "        \"tasks_per_second\": " << worker.tasks_per_second.load() << ",\n";
    oss << "        \"load_factor\": " << std::fixed << std::setprecision(3) << worker.load_factor() << ",\n";
    oss << "        \"utilization_score\": " << std::fixed << std::setprecision(2) << worker.utilization_score() << "\n";
    oss << "      },\n";
    oss << "      \"work_stealing\": {\n";
    oss << "        \"steal_attempts\": " << worker.steal_attempts.load() << ",\n";
    oss << "        \"successful_steals\": " << worker.successful_steals.load() << ",\n";
    oss << "        \"steal_rejections\": " << worker.steal_rejections.load() << ",\n";
    oss << "        \"tasks_stolen_from\": " << worker.tasks_stolen_from.load() << ",\n";
    oss << "        \"tasks_stolen_to\": " << worker.tasks_stolen_to.load() << ",\n";
    oss << "        \"efficiency\": " << std::fixed << std::setprecision(2) << worker.work_stealing_efficiency() << "\n";
    oss << "      },\n";
    oss << "      \"memory\": {\n";
    oss << "        \"stack_usage_bytes\": " << worker.stack_usage_bytes.load() << ",\n";
    oss << "        \"local_heap_bytes\": " << worker.local_heap_bytes.load() << "\n";
    oss << "      }\n";
    oss << "    }";
    
    if (i < worker_stats.size() - 1) {
        oss << ",";
    }
    oss << "\n";
}

oss << "  ]\n}";
return oss.str();
```

}

std::string to_json(const BackendStats& backend_stats) {
std::ostringstream oss;
oss << “{\n”;
oss << “  "io_events": {\n”;
oss << “    "total": “ << backend_stats.total_io_events.load() << “,\n”;
oss << “    "read_events": “ << backend_stats.read_events.load() << “,\n”;
oss << “    "write_events": “ << backend_stats.write_events.load() << “,\n”;
oss << “    "error_events": “ << backend_stats.error_events.load() << “,\n”;
oss << “    "timeout_events": “ << backend_stats.timeout_events.load() << “,\n”;
oss << “    "io_efficiency": “ << std::fixed << std::setprecision(2) << backend_stats.io_efficiency() << “\n”;
oss << “  },\n”;
oss << “  "polling": {\n”;
oss << “    "average_poll_time_ms": “ << std::fixed << std::setprecision(3) << backend_stats.average_poll_time_ms.load() << “,\n”;
oss << “    "poll_calls": “ << backend_stats.poll_calls.load() << “,\n”;
oss << “    "poll_timeouts": “ << backend_stats.poll_timeouts.load() << “,\n”;
oss << “    "events_per_poll": “ << backend_stats.events_per_poll.load() << “\n”;
oss << “  },\n”;
oss << “  "io_uring": {\n”;
oss << “    "submission_queue_depth": “ << backend_stats.submission_queue_depth.load() << “,\n”;
oss << “    "completion_queue_depth": “ << backend_stats.completion_queue_depth.load() << “,\n”;
oss << “    "total_submissions": “ << backend_stats.total_submissions.load() << “,\n”;
oss << “    "total_completions": “ << backend_stats.total_completions.load() << “,\n”;
oss << “    "batch_submissions": “ << backend_stats.batch_submissions.load() << “,\n”;
oss << “    "batch_efficiency": “ << std::fixed << std::setprecision(2) << backend_stats.batch_efficiency.load() << “\n”;
oss << “  },\n”;
oss << “  "connections": {\n”;
oss << “    "active": “ << backend_stats.active_connections.load() << “,\n”;
oss << “    "peak": “ << backend_stats.peak_connections.load() << “,\n”;
oss << “    "total_accepted": “ << backend_stats.total_connections_accepted.load() << “,\n”;
oss << “    "total_closed": “ << backend_stats.connections_closed.load() << “,\n”;
oss << “    "average_duration_ms": “ << std::fixed << std::setprecision(3) << backend_stats.connection_duration_ms.load() << “\n”;
oss << “  },\n”;
oss << “  "throughput": {\n”;
oss << “    "bytes_read": “ << backend_stats.bytes_read.load() << “,\n”;
oss << “    "bytes_written": “ << backend_stats.bytes_written.load() << “,\n”;
oss << “    "read_throughput_mbps": “ << std::fixed << std::setprecision(3) << backend_stats.read_throughput_mbps.load() << “,\n”;
oss << “    "write_throughput_mbps": “ << std::fixed << std::setprecision(3) << backend_stats.write_throughput_mbps.load() << “,\n”;
oss << “    "total_throughput_mbps": “ << std::fixed << std::setprecision(3) << backend_stats.total_throughput_mbps() << “\n”;
oss << “  }\n”;
oss << “}”;
return oss.str();
}

std::string generate_report(const RuntimeStats& runtime_stats,
const std::vector<WorkerStats>& worker_stats,
const BackendStats& backend_stats) {
std::ostringstream oss;

```
// Header
oss << "=============================================\n";
oss << "      STELLANE RUNTIME PERFORMANCE REPORT\n";
oss << "=============================================\n\n";

// Executive Summary
oss << "EXECUTIVE SUMMARY\n";
oss << "-----------------\n";
oss << "Runtime:        " << runtime_stats.backend_type << " backend with " << runtime_stats.worker_count << " workers\n";
oss << "Uptime:         " << (runtime_stats.uptime.count() / 1000.0) << "s\n";
oss << "Tasks/Second:   " << runtime_stats.tasks_per_second.load() << " (peak: " << runtime_stats.peak_tasks_per_second.load() << ")\n";
oss << "Success Rate:   " << std::fixed << std::setprecision(2) << runtime_stats.success_rate() << "%\n";
oss << "Load Factor:    " << std::fixed << std::setprecision(2) << runtime_stats.load_factor() << "\n";
oss << "Memory Usage:   " << (runtime_stats.heap_usage_bytes.load() / (1024 * 1024)) << "MB\n\n";

// Performance Assessment
oss << "PERFORMANCE ASSESSMENT\n";
oss << "----------------------\n";

// Task latency analysis
double avg_latency = runtime_stats.average_task_duration_ms.load();
double p99_latency = runtime_stats.p99_task_duration_ms.load();

if (avg_latency < 1.0) {
    oss << "✓ Latency:      EXCELLENT (avg: " << std::fixed << std::setprecision(3) << avg_latency << "ms)\n";
} else if (avg_latency < 10.0) {
    oss << "✓ Latency:      GOOD (avg: " << std::fixed << std::setprecision(3) << avg_latency << "ms)\n";
} else if (avg_latency < 100.0) {
    oss << "⚠ Latency:      MODERATE (avg: " << std::fixed << std::setprecision(3) << avg_latency << "ms)\n";
} else {
    oss << "✗ Latency:      POOR (avg: " << std::fixed << std::setprecision(3) << avg_latency << "ms)\n";
}

// Throughput analysis
size_t throughput = runtime_stats.tasks_per_second.load();
if (throughput > 10000) {
    oss << "✓ Throughput:   EXCELLENT (" << throughput << " tasks/s)\n";
} else if (throughput > 1000) {
    oss << "✓ Throughput:   GOOD (" << throughput << " tasks/s)\n";
} else if (throughput > 100) {
    oss << "⚠ Throughput:   MODERATE (" << throughput << " tasks/s)\n";
} else {
    oss << "✗ Throughput:   POOR (" << throughput << " tasks/s)\n";
}

// Memory efficiency
double load_factor = runtime_stats.load_factor();
if (load_factor < 0.7) {
    oss << "✓ Load:         OPTIMAL (" << std::fixed << std::setprecision(2) << load_factor << ")\n";
} else if (load_factor < 0.9) {
    oss << "⚠ Load:         HIGH (" << std::fixed << std::setprecision(2) << load_factor << ")\n";
} else {
    oss << "✗ Load:         CRITICAL (" << std::fixed << std::setprecision(2) << load_factor << ")\n";
}

oss << "\n";

// Worker Analysis
oss << "WORKER ANALYSIS\n";
oss << "---------------\n";

// Calculate worker load balance
std::vector<double> worker_loads;
for (const auto& worker : worker_stats) {
    worker_loads.push_back(worker.utilization_score());
}

if (!worker_loads.empty()) {
    double min_load = *std::min_element(worker_loads.begin(), worker_loads.end());
    double max_load = *std::max_element(worker_loads.begin(), worker_loads.end());
    double avg_load = std::accumulate(worker_loads.begin(), worker_loads.end(), 0.0) / worker_loads.size();
    double load_variance = max_load - min_load;
    
    oss << "Load Balance:   ";
    if (load_variance < 10.0) {
        oss << "✓ EXCELLENT (variance: " << std::fixed << std::setprecision(1) << load_variance << "%)\n";
    } else if (load_variance < 25.0) {
        oss << "⚠ MODERATE (variance: " << std::fixed << std::setprecision(1) << load_variance << "%)\n";
    } else {
        oss << "✗ POOR (variance: " << std::fixed << std::setprecision(1) << load_variance << "%)\n";
    }
    
    oss << "Average Load:   " << std::fixed << std::setprecision(1) << avg_load << "%\n";
}

// Work-stealing efficiency
size_t total_steals = 0, total_attempts = 0;
for (const auto& worker : worker_stats) {
    total_steals += worker.successful_steals.load();
    total_attempts += worker.steal_attempts.load();
}

if (total_attempts > 0) {
    double steal_efficiency = (static_cast<double>(total_steals) * 100.0) / total_attempts;
    oss << "Work Stealing:  ";
    if (steal_efficiency > 70.0) {
        oss << "✓ EFFICIENT (" << std::fixed << std::setprecision(1) << steal_efficiency << "%)\n";
    } else if (steal_efficiency > 40.0) {
        oss << "⚠ MODERATE (" << std::fixed << std::setprecision(1) << steal_efficiency << "%)\n";
    } else {
        oss << "✗ INEFFICIENT (" << std::fixed << std::setprecision(1) << steal_efficiency << "%)\n";
    }
}

oss << "\n";

// I/O Performance
oss << "I/O PERFORMANCE\n";
oss << "---------------\n";
oss << "Total Events:   " << backend_stats.total_io_events.load() << "\n";
oss << "Read Events:    " << backend_stats.read_events.load() << "\n";
oss << "Write Events:   " << backend_stats.write_events.load() << "\n";
oss << "Error Events:   " << backend_stats.error_events.load() << "\n";
oss << "I/O Efficiency: " << std::fixed << std::setprecision(2) << backend_stats.io_efficiency() << "%\n";
oss << "Read Throughput:" << std::fixed << std::setprecision(2) << backend_stats.read_throughput_mbps.load() << " MB/s\n";
oss << "Write Throughput:" << std::fixed << std::setprecision(2) << backend_stats.write_throughput_mbps.load() << " MB/s\n";
oss << "Total Throughput:" << std::fixed << std::setprecision(2) << backend_stats.total_throughput_mbps() << " MB/s\n\n";

// Recommendations
oss << "RECOMMENDATIONS\n";
oss << "---------------\n";

if (avg_latency > 50.0) {
    oss << "• Consider enabling CPU affinity to reduce context switching\n";
    oss << "• Reduce max_tasks_per_loop for lower latency\n";
}

if (load_factor > 0.8) {
    oss << "• Scale up worker threads or reduce incoming load\n";
    oss << "• Enable work-stealing if not already active\n";
}

if (load_variance > 20.0) {
    oss << "• Enable work-stealing for better load distribution\n";
    oss << "• Consider CPU affinity optimization\n";
}

if (backend_stats.io_efficiency() < 90.0) {
    oss << "• Investigate error events causing I/O inefficiency\n";
    oss << "• Consider increasing I/O batch size\n";
}

if (runtime_stats.heap_usage_bytes.load() > (512 * 1024 * 1024)) { // 512MB
    oss << "• Monitor memory usage for potential leaks\n";
    oss << "• Consider enabling task pooling\n";
}

// Recovery stats (if applicable)
if (runtime_stats.total_recovery_attempts.load() > 0) {
    oss << "\nRECOVERY SYSTEM\n";
    oss << "---------------\n";
    oss << "Total Attempts: " << runtime_stats.total_recovery_attempts.load() << "\n";
    oss << "Success Rate:   " << std::fixed << std::setprecision(2) << runtime_stats.recovery_success_rate() << "%\n";
    oss << "Avg Time:       " << std::fixed << std::setprecision(3) << runtime_stats.average_recovery_time_ms.load() << "ms\n";
    
    if (runtime_stats.recovery_success_rate() < 90.0) {
        oss << "\n• Review recovery configuration - low success rate detected\n";
    }
}

oss << "\n=============================================\n";
oss << "Report generated at: " << std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count() << "\n";
oss << "=============================================\n";

return oss.str();
```

}

bool export_to_csv(const std::string& filename,
const RuntimeStats& runtime_stats,
const std::vector<WorkerStats>& worker_stats) {
std::ofstream file(filename);
if (!file.is_open()) {
return false;
}

```
// Write CSV header
file << "timestamp,metric_type,metric_name,value,unit,worker_id\n";

auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();

// Runtime metrics
file << timestamp << ",runtime,total_tasks_executed," << runtime_stats.total_tasks_executed.load() << ",count,-1\n";
file << timestamp << ",runtime,total_tasks_failed," << runtime_stats.total_tasks_failed.load() << ",count,-1\n";
file << timestamp << ",runtime,current_active_tasks," << runtime_stats.current_active_tasks.load() << ",count,-1\n";
file << timestamp << ",runtime,current_pending_tasks," << runtime_stats.current_pending_tasks.load() << ",count,-1\n";
file << timestamp << ",runtime,tasks_per_second," << runtime_stats.tasks_per_second.load() << ",tps,-1\n";
file << timestamp << ",runtime,average_task_duration_ms," << runtime_stats.average_task_duration_ms.load() << ",ms,-1\n";
file << timestamp << ",runtime,p95_task_duration_ms," << runtime_stats.p95_task_duration_ms.load() << ",ms,-1\n";
file << timestamp << ",runtime,p99_task_duration_ms," << runtime_stats.p99_task_duration_ms.load() << ",ms,-1\n";
file << timestamp << ",runtime,heap_usage_bytes," << runtime_stats.heap_usage_bytes.load() << ",bytes,-1\n";
file << timestamp << ",runtime,load_factor," << runtime_stats.load_factor() << ",ratio,-1\n";
file << timestamp << ",runtime,success_rate," << runtime_stats.success_rate() << ",percent,-1\n";

// Worker metrics
for (const auto& worker : worker_stats) {
    file << timestamp << ",worker,tasks_executed," << worker.tasks_executed.load() << ",count," << worker.worker_id << "\n";
    file << timestamp << ",worker,tasks_failed," << worker.tasks_failed.load() << ",count," << worker.worker_id << "\n";
    file << timestamp << ",worker,current_queue_size," << worker.current_queue_size.load() << ",count," << worker.worker_id << "\n";
    file << timestamp << ",worker,cpu_usage_percent," << worker.cpu_usage_percent.load() << ",percent," << worker.worker_id << "\n";
    file << timestamp << ",worker,tasks_per_second," << worker.tasks_per_second.load() << ",tps," << worker.worker_id << "\n";
    file << timestamp << ",worker,load_factor," << worker.load_factor() << ",ratio," << worker.worker_id << "\n";
    file << timestamp << ",worker,successful_steals," << worker.successful_steals.load() << ",count," << worker.worker_id << "\n";
    file << timestamp << ",worker,steal_attempts," << worker.steal_attempts.load() << ",count," << worker.worker_id << "\n";
    file << timestamp << ",worker,work_stealing_efficiency," << worker.work_stealing_efficiency() << ",percent," << worker.worker_id << "\n";
}

return true;
```

}

} // namespace stats_formatter

// ============================================================================
// Global Statistics Collector Instance
// ============================================================================

namespace {
std::unique_ptr<IStatsCollector> g_stats_collector;
std::mutex g_stats_collector_mutex;
}

// ============================================================================
// Public API Functions
// ============================================================================

void initialize_stats_collector(size_t worker_count, const std::string& backend_type) {
std::lock_guard<std::mutex> lock(g_stats_collector_mutex);
g_stats_collector = std::make_unique<DefaultStatsCollector>(worker_count, backend_type);
}

void shutdown_stats_collector() {
std::lock_guard<std::mutex> lock(g_stats_collector_mutex);
g_stats_collector.reset();
}

IStatsCollector* get_stats_collector() {
std::lock_guard<std::mutex> lock(g_stats_collector_mutex);
return g_stats_collector.get();
}

void record_task_execution(int worker_id, double duration_ms, bool success) {
if (auto collector = get_stats_collector()) {
collector->record_task_execution(worker_id, duration_ms, success);
}
}

void record_io_event(const std::string& event_type, size_t bytes_processed) {
if (auto collector = get_stats_collector()) {
collector->record_io_event(event_type, bytes_processed);
}
}

void record_allocation(size_t size_bytes) {
if (auto collector = get_stats_collector()) {
collector->record_allocation(size_bytes);
}
}

void record_deallocation(size_t size_bytes) {
if (auto collector = get_stats_collector()) {
collector->record_deallocation(size_bytes);
}
}

RuntimeStats get_runtime_stats() {
if (auto collector = get_stats_collector()) {
return collector->get_runtime_stats();
}
return RuntimeStats{};
}

std::vector<WorkerStats> get_worker_stats() {
if (auto collector = get_stats_collector()) {
return collector->get_worker_stats();
}
return {};
}

BackendStats get_backend_stats() {
if (auto collector = get_stats_collector()) {
return collector->get_backend_stats();
}
return BackendStats{};
}

void enable_profiling(bool enabled) {
if (auto collector = get_stats_collector()) {
collector->enable_profiling(enabled);
}
}

void reset_all_stats() {
if (auto collector = get_stats_collector()) {
collector->reset_stats();
}
}

void update_stats_periodic() {
if (auto collector = get_stats_collector()) {
collector->update_stats();
}
}


// ============================================================================
// Convenience Functions for Common Operations
// ============================================================================

/**

- @brief Generate a quick health check report
  */
  std::string generate_health_check() {
  auto runtime_stats = get_runtime_stats();
  auto worker_stats = get_worker_stats();
  
  std::ostringstream oss;
  oss << “HEALTH CHECK\n”;
  oss << “============\n”;
  
  // Overall health
  bool healthy = true;
  std::vector<std::string> issues;
  std::vector<std::string> warnings;
  
  // Check failure rate
  if (runtime_stats.success_rate() < 95.0) {
  issues.push_back(“High failure rate: “ + std::to_string(runtime_stats.success_rate()) + “%”);
  healthy = false;
  } else if (runtime_stats.success_rate() < 99.0) {
  warnings.push_back(“Moderate failure rate: “ + std::to_string(runtime_stats.success_rate()) + “%”);
  }
  
  // Check load
  if (runtime_stats.load_factor() > 0.9) {
  issues.push_back(“Critical load: “ + std::to_string(runtime_stats.load_factor()));
  healthy = false;
  } else if (runtime_stats.load_factor() > 0.7) {
  warnings.push_back(“High load: “ + std::to_string(runtime_stats.load_factor()));
  }
  
  // Check latency
  double avg_latency = runtime_stats.average_task_duration_ms.load();
  if (avg_latency > 1000.0) {
  issues.push_back(“Very high latency: “ + std::to_string(avg_latency) + “ms”);
  healthy = false;
  } else if (avg_latency > 100.0) {
  warnings.push_back(“High latency: “ + std::to_string(avg_latency) + “ms”);
  }
  
  // Check memory usage
  size_t memory_mb = runtime_stats.heap_usage_bytes.load() / (1024 * 1024);
  if (memory_mb > 1024) { // > 1GB
  warnings.push_back(“High memory usage: “ + std::to_string(memory_mb) + “MB”);
  }
  
  // Check for stuck workers
  auto now = std::chrono::steady_clock::now();
  int idle_workers = 0;
  for (const auto& worker : worker_stats) {
  auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(
  now - worker.last_activity).count();
  if (idle_time > 60) { // 60 seconds idle
  idle_workers++;
  if (idle_time > 300) { // 5 minutes
  issues.push_back(“Worker “ + std::to_string(worker.worker_id) + “ stuck for “ + std::to_string(idle_time) + “s”);
  healthy = false;
  } else {
  warnings.push_back(“Worker “ + std::to_string(worker.worker_id) + “ idle for “ + std::to_string(idle_time) + “s”);
  }
  }
  }
  
  // Overall status
  if (healthy) {
  oss << “✅ STATUS: HEALTHY\n\n”;
  } else {
  oss << “❌ STATUS: UNHEALTHY\n\n”;
  }
  
  // Report issues
  if (!issues.empty()) {
  oss << “CRITICAL ISSUES:\n”;
  for (const auto& issue : issues) {
  oss << “  ❌ “ << issue << “\n”;
  }
  oss << “\n”;
  }
  
  if (!warnings.empty()) {
  oss << “WARNINGS:\n”;
  for (const auto& warning : warnings) {
  oss << “  ⚠️  “ << warning << “\n”;
  }
  oss << “\n”;
  }
  
  // Key metrics summary
  oss << “KEY METRICS:\n”;
  oss << “  Uptime: “ << (runtime_stats.uptime.count() / 1000.0) << “s\n”;
  oss << “  Tasks/sec: “ << runtime_stats.tasks_per_second.load() << “\n”;
  oss << “  Success rate: “ << std::fixed << std::setprecision(2) << runtime_stats.success_rate() << “%\n”;
  oss << “  Avg latency: “ << std::fixed << std::setprecision(3) << avg_latency << “ms\n”;
  oss << “  Active workers: “ << (worker_stats.size() - idle_workers) << “/” << worker_stats.size() << “\n”;
  oss << “  Memory: “ << memory_mb << “MB\n”;
  
  return oss.str();
  }

/**

- @brief Get performance alerts based on thresholds
  */
  std::vector<std::string> get_performance_alerts() {
  std::vector<std::string> alerts;
  
  auto runtime_stats = get_runtime_stats();
  auto worker_stats = get_worker_stats();
  auto backend_stats = get_backend_stats();
  
  // Task execution alerts
  if (runtime_stats.success_rate() < 95.0) {
  alerts.push_back(“CRITICAL: Task failure rate exceeds 5% (” +
  std::to_string(runtime_stats.success_rate()) + “%)”);
  }
  
  if (runtime_stats.tasks_per_second.load() == 0 && runtime_stats.uptime.count() > 10000) {
  alerts.push_back(“CRITICAL: Zero task throughput detected”);
  }
  
  // Latency alerts
  double p99_latency = runtime_stats.p99_task_duration_ms.load();
  if (p99_latency > 1000.0) {
  alerts.push_back(“WARNING: P99 latency exceeds 1s (” + std::to_string(p99_latency) + “ms)”);
  }
  
  // Load alerts
  if (runtime_stats.load_factor() > 0.95) {
  alerts.push_back(“CRITICAL: System load exceeds 95% (” + std::to_string(runtime_stats.load_factor()) + “)”);
  }
  
  // Memory alerts
  size_t memory_mb = runtime_stats.heap_usage_bytes.load() / (1024 * 1024);
  if (memory_mb > 2048) { // > 2GB
  alerts.push_back(“WARNING: High memory usage (” + std::to_string(memory_mb) + “MB)”);
  }
  
  // Worker imbalance alerts
  if (worker_stats.size() > 1) {
  std::vector<double> worker_loads;
  for (const auto& worker : worker_stats) {
  worker_loads.push_back(worker.utilization_score());
  }
  
  ```
   double min_load = *std::min_element(worker_loads.begin(), worker_loads.end());
   double max_load = *std::max_element(worker_loads.begin(), worker_loads.end());
   double load_variance = max_load - min_load;
   
   if (load_variance > 50.0) {
       alerts.push_back("WARNING: High worker load imbalance (variance: " + 
                      std::to_string(load_variance) + "%)");
   }
  ```
  
  }
  
  // I/O efficiency alerts
  if (backend_stats.io_efficiency() < 80.0 && backend_stats.total_io_events.load() > 1000) {
  alerts.push_back(“WARNING: Low I/O efficiency (” +
  std::to_string(backend_stats.io_efficiency()) + “%)”);
  }
  
  // Connection alerts
  size_t active_connections = backend_stats.active_connections.load();
  size_t peak_connections = backend_stats.peak_connections.load();
  if (active_connections > 0 && peak_connections > 0) {
  double connection_utilization = static_cast<double>(active_connections) / peak_connections;
  if (connection_utilization > 0.9) {
  alerts.push_back(“WARNING: High connection utilization (” +
  std::to_string(connection_utilization * 100.0) + “%)”);
  }
  }
  
  // Recovery system alerts
  if (runtime_stats.total_recovery_attempts.load() > 0 && runtime_stats.recovery_success_rate() < 80.0) {
  alerts.push_back(“CRITICAL: Low recovery success rate (” +
  std::to_string(runtime_stats.recovery_success_rate()) + “%)”);
  }
  
  return alerts;
  }

/**

- @brief Calculate system efficiency score (0-100)
  */
  double calculate_efficiency_score() {
  auto runtime_stats = get_runtime_stats();
  auto worker_stats = get_worker_stats();
  auto backend_stats = get_backend_stats();
  
  double total_score = 0.0;
  int component_count = 0;
  
  // Success rate component (25% weight)
  double success_score = runtime_stats.success_rate();
  total_score += success_score * 0.25;
  component_count++;
  
  // Latency component (20% weight)
  double avg_latency = runtime_stats.average_task_duration_ms.load();
  double latency_score = 100.0;
  if (avg_latency > 1.0) {
  latency_score = std::max(0.0, 100.0 - (avg_latency - 1.0) * 2.0); // Penalty for latency > 1ms
  }
  total_score += latency_score * 0.20;
  component_count++;
  
  // Load balance component (15% weight)
  double load_balance_score = 100.0;
  if (worker_stats.size() > 1) {
  std::vector<double> worker_loads;
  for (const auto& worker : worker_stats) {
  worker_loads.push_back(worker.utilization_score());
  }
  
  ```
   double min_load = *std::min_element(worker_loads.begin(), worker_loads.end());
   double max_load = *std::max_element(worker_loads.begin(), worker_loads.end());
   double load_variance = max_load - min_load;
   
   load_balance_score = std::max(0.0, 100.0 - load_variance);
  ```
  
  }
  total_score += load_balance_score * 0.15;
  component_count++;
  
  // I/O efficiency component (15% weight)
  double io_score = backend_stats.io_efficiency();
  total_score += io_score * 0.15;
  component_count++;
  
  // Resource utilization component (15% weight)
  double load_factor = runtime_stats.load_factor();
  double resource_score = 100.0;
  if (load_factor > 0.8) {
  resource_score = std::max(0.0, 100.0 - (load_factor - 0.8) * 500.0); // Penalty for high load
  }
  total_score += resource_score * 0.15;
  component_count++;
  
  // Memory efficiency component (10% weight)
  size_t memory_mb = runtime_stats.heap_usage_bytes.load() / (1024 * 1024);
  double memory_score = 100.0;
  if (memory_mb > 512) { // Penalty for memory > 512MB
  memory_score = std::max(0.0, 100.0 - (memory_mb - 512) * 0.1);
  }
  total_score += memory_score * 0.10;
  component_count++;
  
  return component_count > 0 ? total_score : 0.0;
  }

/**

- @brief Generate performance trend analysis
  */
  std::string generate_trend_analysis(const std::vector<RuntimeStats>& historical_stats) {
  if (historical_stats.size() < 2) {
  return “Insufficient data for trend analysis (need at least 2 data points)”;
  }
  
  std::ostringstream oss;
  oss << “PERFORMANCE TREND ANALYSIS\n”;
  oss << “==========================\n\n”;
  
  const auto& latest = historical_stats.back();
  const auto& previous = historical_stats[historical_stats.size() - 2];
  
  // Calculate trends
  auto throughput_change = static_cast<double>(latest.tasks_per_second.load()) -
  static_cast<double>(previous.tasks_per_second.load());
  auto latency_change = latest.average_task_duration_ms.load() - previous.average_task_duration_ms.load();
  auto success_rate_change = latest.success_rate() - previous.success_rate();
  auto memory_change = static_cast<double>(latest.heap_usage_bytes.load()) -
  static_cast<double>(previous.heap_usage_bytes.load());
  
  // Throughput trend
  oss << “Throughput: “;
  if (throughput_change > 0) {
  oss << “📈 IMPROVING (+” << throughput_change << “ tasks/s)\n”;
  } else if (throughput_change < 0) {
  oss << “📉 DECLINING (” << throughput_change << “ tasks/s)\n”;
  } else {
  oss << “➡️  STABLE\n”;
  }
  
  // Latency trend
  oss << “Latency: “;
  if (latency_change > 0.1) {
  oss << “📈 WORSENING (+” << std::fixed << std::setprecision(3) << latency_change << “ms)\n”;
  } else if (latency_change < -0.1) {
  oss << “📉 IMPROVING (” << std::fixed << std::setprecision(3) << latency_change << “ms)\n”;
  } else {
  oss << “➡️  STABLE\n”;
  }
  
  // Success rate trend
  oss << “Success Rate: “;
  if (success_rate_change > 0.1) {
  oss << “📈 IMPROVING (+” << std::fixed << std::setprecision(2) << success_rate_change << “%)\n”;
  } else if (success_rate_change < -0.1) {
  oss << “📉 DECLINING (” << std::fixed << std::setprecision(2) << success_rate_change << “%)\n”;
  } else {
  oss << “➡️  STABLE\n”;
  }
  
  // Memory trend
  oss << “Memory Usage: “;
  double memory_change_mb = memory_change / (1024.0 * 1024.0);
  if (memory_change_mb > 1.0) {
  oss << “📈 INCREASING (+” << std::fixed << std::setprecision(1) << memory_change_mb << “MB)\n”;
  } else if (memory_change_mb < -1.0) {
  oss << “📉 DECREASING (” << std::fixed << std::setprecision(1) << memory_change_mb << “MB)\n”;
  } else {
  oss << “➡️  STABLE\n”;
  }
  
  // Overall assessment
  oss << “\nOVERALL TREND: “;
  int positive_trends = 0;
  int negative_trends = 0;
  
  if (throughput_change > 0) positive_trends++;
  else if (throughput_change < 0) negative_trends++;
  
  if (latency_change < -0.1) positive_trends++;
  else if (latency_change > 0.1) negative_trends++;
  
  if (success_rate_change > 0.1) positive_trends++;
  else if (success_rate_change < -0.1) negative_trends++;
  
  if (positive_trends > negative_trends) {
  oss << “📈 IMPROVING”;
  } else if (negative_trends > positive_trends) {
  oss << “📉 CONCERNING”;
  } else {
  oss << “➡️  STABLE”;
  }
  
  oss << “\n”;
  
  return oss.str();
  }

/**

- @brief Export statistics in Prometheus format
  */
  std::string export_prometheus_metrics() {
  auto runtime_stats = get_runtime_stats();
  auto worker_stats = get_worker_stats();
  auto backend_stats = get_backend_stats();
  
  std::ostringstream oss;
  
  // Runtime metrics
  oss << “# HELP stellane_runtime_uptime_seconds Runtime uptime in seconds\n”;
  oss << “# TYPE stellane_runtime_uptime_seconds counter\n”;
  oss << “stellane_runtime_uptime_seconds “ << (runtime_stats.uptime.count() / 1000.0) << “\n\n”;
  
  oss << “# HELP stellane_tasks_total Total number of tasks executed\n”;
  oss << “# TYPE stellane_tasks_total counter\n”;
  oss << “stellane_tasks_total{status="executed"} “ << runtime_stats.total_tasks_executed.load() << “\n”;
  oss << “stellane_tasks_total{status="failed"} “ << runtime_stats.total_tasks_failed.load() << “\n\n”;
  
  oss << “# HELP stellane_tasks_current Current number of tasks\n”;
  oss << “# TYPE stellane_tasks_current gauge\n”;
  oss << “stellane_tasks_current{status="active"} “ << runtime_stats.current_active_tasks.load() << “\n”;
  oss << “stellane_tasks_current{status="pending"} “ << runtime_stats.current_pending_tasks.load() << “\n\n”;
  
  oss << “# HELP stellane_tasks_per_second Current task processing rate\n”;
  oss << “# TYPE stellane_tasks_per_second gauge\n”;
  oss << “stellane_tasks_per_second “ << runtime_stats.tasks_per_second.load() << “\n\n”;
  
  oss << “# HELP stellane_task_duration_seconds Task execution duration\n”;
  oss << “# TYPE stellane_task_duration_seconds histogram\n”;
  oss << “stellane_task_duration_seconds{quantile="0.5"} “ << (runtime_stats.average_task_duration_ms.load() / 1000.0) << “\n”;
  oss << “stellane_task_duration_seconds{quantile="0.95"} “ << (runtime_stats.p95_task_duration_ms.load() / 1000.0) << “\n”;
  oss << “stellane_task_duration_seconds{quantile="0.99"} “ << (runtime_stats.p99_task_duration_ms.load() / 1000.0) << “\n\n”;
  
  oss << “# HELP stellane_memory_usage_bytes Memory usage in bytes\n”;
  oss << “# TYPE stellane_memory_usage_bytes gauge\n”;
  oss << “stellane_memory_usage_bytes{type="heap"} “ << runtime_stats.heap_usage_bytes.load() << “\n”;
  oss << “stellane_memory_usage_bytes{type="stack"} “ << runtime_stats.stack_usage_bytes.load() << “\n\n”;
  
  // Worker metrics
  oss << “# HELP stellane_worker_tasks_total Total tasks processed by worker\n”;
  oss << “# TYPE stellane_worker_tasks_total counter\n”;
  for (const auto& worker : worker_stats) {
  oss << “stellane_worker_tasks_total{worker_id="” << worker.worker_id << “",status="executed"} “
  << worker.tasks_executed.load() << “\n”;
  oss << “stellane_worker_tasks_total{worker_id="” << worker.worker_id << “",status="failed"} “
  << worker.tasks_failed.load() << “\n”;
  }
  oss << “\n”;
  
  oss << “# HELP stellane_worker_cpu_usage_percent Worker CPU usage percentage\n”;
  oss << “# TYPE stellane_worker_cpu_usage_percent gauge\n”;
  for (const auto& worker : worker_stats) {
  oss << “stellane_worker_cpu_usage_percent{worker_id="” << worker.worker_id << “"} “
  << worker.cpu_usage_percent.load() << “\n”;
  }
  oss << “\n”;
  
  oss << “# HELP stellane_worker_queue_size Current worker queue size\n”;
  oss << “# TYPE stellane_worker_queue_size gauge\n”;
  for (const auto& worker : worker_stats) {
  oss << “stellane_worker_queue_size{worker_id="” << worker.worker_id << “"} “
  << worker.current_queue_size.load() << “\n”;
  }
  oss << “\n”;
  
  // I/O metrics
  oss << “# HELP stellane_io_events_total Total I/O events processed\n”;
  oss << “# TYPE stellane_io_events_total counter\n”;
  oss << “stellane_io_events_total{type="read"} “ << backend_stats.read_events.load() << “\n”;
  oss << “stellane_io_events_total{type="write"} “ << backend_stats.write_events.load() << “\n”;
  oss << “stellane_io_events_total{type="error"} “ << backend_stats.error_events.load() << “\n\n”;
  
  oss << “# HELP stellane_io_bytes_total Total I/O bytes processed\n”;
  oss << “# TYPE stellane_io_bytes_total counter\n”;
  oss << “stellane_io_bytes_total{direction="read"} “ << backend_stats.bytes_read.load() << “\n”;
  oss << “stellane_io_bytes_total{direction="write"} “ << backend_stats.bytes_written.load() << “\n\n”;
  
  oss << “# HELP stellane_connections_current Current number of connections\n”;
  oss << “# TYPE stellane_connections_current gauge\n”;
  oss << “stellane_connections_current “ << backend_stats.active_connections.load() << “\n\n”;
  
  return oss.str();
  }

/**

- @brief Schedule periodic statistics updates
  */
  void start_periodic_stats_update(std::chrono::milliseconds interval) {
  static std::atomic<bool> running{false};
  static std::thread update_thread;
  
  if (running.exchange(true)) {
  return; // Already running
  }
  
  update_thread = std::thread([interval]() {
  while (running.load()) {
  update_stats_periodic();
  std::this_thread::sleep_for(interval);
  }
  });
  
  update_thread.detach();
  }

/**

- @brief Stop periodic statistics updates
  */
  void stop_periodic_stats_update() {
  static std::atomic<bool> running{false};
  running.store(false);
  }

} // namespace stellane::runtime

// ============================================================================
// Template Specializations and Additional Utilities
// ============================================================================

namespace stellane::runtime {

/**

- @brief RAII helper for automatic task timing
  */
  class TaskTimer {
  private:
  std::chrono::steady_clock::time_point start_time_;
  int worker_id_;
  bool completed_;

public:
explicit TaskTimer(int worker_id = -1)
: start_time_(std::chrono::steady_clock::now())
, worker_id_(worker_id)
, completed_(false) {}

```
~TaskTimer() {
    if (!completed_) {
        complete(true); // Default to successful completion
    }
}

void complete(bool success = true) {
    if (completed_) return;
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time_).count() / 1000.0; // Convert to milliseconds
    
    record_task_execution(worker_id_, duration, success);
    completed_ = true;
}

double elapsed_ms() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_time_).count() / 1000.0;
}
```

};

/**

- @brief RAII helper for memory tracking
  */
  class MemoryTracker {
  private:
  size_t allocated_bytes_;

public:
explicit MemoryTracker(size_t bytes) : allocated_bytes_(bytes) {
record_allocation(allocated_bytes_);
}

```
~MemoryTracker() {
    record_deallocation(allocated_bytes_);
}

MemoryTracker(const MemoryTracker&) = delete;
MemoryTracker& operator=(const MemoryTracker&) = delete;

MemoryTracker(MemoryTracker&& other) noexcept 
    : allocated_bytes_(other.allocated_bytes_) {
    other.allocated_bytes_ = 0;
}

MemoryTracker& operator=(MemoryTracker&& other) noexcept {
    if (this != &other) {
        if (allocated_bytes_ > 0) {
            record_deallocation(allocated_bytes_);
        }
        allocated_bytes_ = other.allocated_bytes_;
        other.allocated_bytes_ = 0;
    }
    return *this;
}
```

};

/**

- @brief Macro for easy task timing
  */
  #define STELLANE_TASK_TIMER(worker_id)   
  stellane::runtime::TaskTimer _stellane_timer(worker_id)

#define STELLANE_TASK_TIMER_COMPLETE(success)   
_stellane_timer.complete(success)

/**

- @brief Macro for memory tracking
  */
  #define STELLANE_MEMORY_TRACKER(bytes)   
  stellane::runtime::MemoryTracker _stellane_mem_tracker(bytes)

} // namespace stellane::runtime
