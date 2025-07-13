#pragma once

#include “stellane/runtime/config/config_enums.h”
#include <vector>
#include <string>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <thread>

namespace stellane {

// ============================================================================
// CPU Affinity Configuration
// ============================================================================

/**

- @brief CPU affinity configuration for worker threads
- 
- Controls how worker threads are bound to specific CPU cores
- for optimal cache locality and reduced context switching.
  */
  struct AffinityConfig {
  AffinityStrategy strategy = AffinityStrategy::NONE;  ///< Affinity assignment strategy
  
  /**
  - @brief Explicit CPU core assignments
  - 
  - Map of worker_id -> cpu_core. If empty, automatic assignment is used.
  - Only used when strategy is CUSTOM.
    */
    std::unordered_map<int, int> explicit_mappings;
  
  /**
  - @brief CPU cores to exclude from automatic assignment
  - 
  - Useful for reserving cores for system processes or other applications.
    */
    std::vector<int> excluded_cores;
  
  /**
  - @brief Whether to isolate the main thread to a separate core
    */
    bool isolate_main_thread = false;
  
  /**
  - @brief CPU core for main thread (if isolate_main_thread is true)
  - 
  - If not specified, the last available core is used.
    */
    std::optional<int> main_thread_core;
  
  /**
  - @brief Whether to respect NUMA topology when assigning cores
    */
    bool respect_numa_topology = true;
  
  /**
  - @brief Allow threads to migrate between assigned cores
  - 
  - If false, threads are strictly bound to their assigned core.
  - If true, threads can migrate within their NUMA node.
    */
    bool allow_migration = false;
  
  /**
  - @brief Get the CPU core assignment for a worker
  - @param worker_id Worker thread identifier
  - @param total_workers Total number of worker threads
  - @return CPU core number, or -1 if no specific assignment
    */
    [[nodiscard]] int get_cpu_core(int worker_id, size_t total_workers) const {
    // Custom explicit mapping takes precedence
    auto it = explicit_mappings.find(worker_id);
    if (it != explicit_mappings.end()) {
    return it->second;
    }
    
    // Automatic assignment based on strategy
    switch (strategy) {
    case AffinityStrategy::NONE:
    return -1; // No affinity
    
    ```
     case AffinityStrategy::ROUND_ROBIN: {
         size_t available_cores = std::thread::hardware_concurrency();
         for (int excluded : excluded_cores) {
             if (excluded >= 0 && excluded < static_cast<int>(available_cores)) {
                 available_cores--;
             }
         }
         return available_cores > 0 ? (worker_id % available_cores) : -1;
     }
     
     case AffinityStrategy::NUMA_AWARE:
         // NUMA-aware assignment would be implemented in .cpp
         return -1; // Placeholder
         
     case AffinityStrategy::CUSTOM:
         return -1; // Requires explicit mapping
         
     default:
         return -1;
    ```
    
    }
    }
  
  /**
  - @brief Validate affinity configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    size_t hardware_cores = std::thread::hardware_concurrency();
    
    // Check explicit mappings
    for (const auto& [worker_id, cpu_core] : explicit_mappings) {
    if (worker_id < 0 || cpu_core < 0 || cpu_core >= static_cast<int>(hardware_cores)) {
    return false;
    }
    }
    
    // Check excluded cores
    for (int core : excluded_cores) {
    if (core < 0 || core >= static_cast<int>(hardware_cores)) {
    return false;
    }
    }
    
    // Check main thread core
    if (main_thread_core && (*main_thread_core < 0 || *main_thread_core >= static_cast<int>(hardware_cores))) {
    return false;
    }
    
    return true;
    }
    };

// ============================================================================
// Work-Stealing Configuration
// ============================================================================

/**

- @brief Work-stealing configuration for load balancing
- 
- Controls how idle workers steal tasks from busy workers
- to maintain optimal load distribution across cores.
  */
  struct WorkStealingConfig {
  WorkStealingPolicy policy = WorkStealingPolicy::CONSERVATIVE;  ///< Work stealing policy
  
  /**
  - @brief Whether work stealing is enabled
    */
    bool enabled = true;
  
  /**
  - @brief Minimum number of tasks before allowing stealing
  - 
  - Workers with fewer tasks than this threshold won’t have tasks stolen.
    */
    size_t steal_threshold = 10;
  
  /**
  - @brief How often to attempt work stealing (in microseconds)
    */
    std::chrono::microseconds steal_interval{100};
  
  /**
  - @brief Maximum number of steal attempts per interval
    */
    size_t max_steal_attempts = 3;
  
  /**
  - @brief Prefer stealing from specific workers (NUMA locality)
    */
    bool prefer_numa_local_stealing = true;
  
  /**
  - @brief Steal from front (FIFO) or back (LIFO) of victim’s queue
  - 
  - LIFO stealing typically provides better cache locality.
    */
    bool steal_from_back = true;
  
  /**
  - @brief Maximum number of tasks to steal in one attempt
    */
    size_t max_tasks_per_steal = 1;
  
  /**
  - @brief Adaptive parameters for ADAPTIVE policy
    */
    struct AdaptiveParams {
    double load_threshold = 0.7;               ///< Load threshold to trigger stealing
    std::chrono::seconds measurement_window{5}; ///< Window for load measurement
    double steal_rate_adjustment = 0.1;        ///< Rate of steal frequency adjustment
    size_t min_steal_interval_us = 50;         ///< Minimum steal interval (microseconds)
    size_t max_steal_interval_us = 1000;       ///< Maximum steal interval (microseconds)
    } adaptive;
  
  /**
  - @brief Get effective steal interval based on policy and system state
  - @param current_load Current system load (0.0 - 1.0)
  - @return Effective steal interval in microseconds
    */
    [[nodiscard]] std::chrono::microseconds get_effective_interval(double current_load = 0.5) const {
    switch (policy) {
    case WorkStealingPolicy::DISABLED:
    return std::chrono::microseconds::max(); // Never steal
    
    ```
     case WorkStealingPolicy::AGGRESSIVE:
         return std::chrono::microseconds(steal_interval.count() / 2); // Double frequency
         
     case WorkStealingPolicy::CONSERVATIVE:
         return steal_interval; // Use configured interval
         
     case WorkStealingPolicy::ADAPTIVE:
         if (current_load > adaptive.load_threshold) {
             // High load: steal more aggressively
             auto adjusted = static_cast<size_t>(steal_interval.count() * (1.0 - current_load));
             return std::chrono::microseconds(
                 std::clamp(adjusted, adaptive.min_steal_interval_us, adaptive.max_steal_interval_us)
             );
         } else {
             // Low load: steal less frequently
             return std::chrono::microseconds(
                 std::min(steal_interval.count() * 2, adaptive.max_steal_interval_us)
             );
         }
         
     case WorkStealingPolicy::CUSTOM:
         return steal_interval; // Use configured interval
         
     default:
         return steal_interval;
    ```
    
    }
    }
  
  /**
  - @brief Validate work-stealing configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    return steal_threshold > 0 &&
    steal_interval.count() > 0 &&
    max_steal_attempts > 0 &&
    max_tasks_per_steal > 0 &&
    adaptive.load_threshold >= 0.0 && adaptive.load_threshold <= 1.0 &&
    adaptive.measurement_window.count() > 0 &&
    adaptive.min_steal_interval_us > 0 &&
    adaptive.max_steal_interval_us >= adaptive.min_steal_interval_us;
    }
    };

// ============================================================================
// NUMA Configuration
// ============================================================================

/**

- @brief NUMA (Non-Uniform Memory Access) configuration
- 
- Optimizes memory access patterns for multi-socket systems
- by keeping threads and memory on the same NUMA node.
  */
  struct NumaConfig {
  /**
  - @brief Whether NUMA optimizations are enabled
    */
    bool enabled = false;
  
  /**
  - @brief Automatically detect NUMA topology
    */
    bool auto_detect_topology = true;
  
  /**
  - @brief Explicit NUMA node assignments for workers
  - 
  - Map of worker_id -> numa_node. If empty, automatic assignment is used.
    */
    std::unordered_map<int, int> node_assignments;
  
  /**
  - @brief Memory allocation policy
    */
    enum class MemoryPolicy {
    DEFAULT = 0,        ///< Use system default
    BIND = 1,          ///< Bind to specific NUMA node
    INTERLEAVE = 2,    ///< Interleave across nodes
    PREFERRED = 3      ///< Prefer specific node but allow fallback
    };
  
  MemoryPolicy memory_policy = MemoryPolicy::PREFERRED;
  
  /**
  - @brief Whether to balance workers across NUMA nodes
    */
    bool balance_workers = true;
  
  /**
  - @brief Minimum number of workers per NUMA node
    */
    size_t min_workers_per_node = 1;
  
  /**
  - @brief Allow cross-NUMA memory access for work stealing
    */
    bool allow_cross_numa_stealing = false;
  
  /**
  - @brief NUMA node preferences for different workload types
    */
    struct NodePreferences {
    std::optional<int> io_intensive_node;      ///< Preferred node for I/O-heavy tasks
    std::optional<int> cpu_intensive_node;     ///< Preferred node for CPU-heavy tasks
    std::optional<int> memory_intensive_node;  ///< Preferred node for memory-heavy tasks
    } preferences;
  
  /**
  - @brief Get NUMA node assignment for a worker
  - @param worker_id Worker thread identifier
  - @param total_workers Total number of worker threads
  - @param available_nodes Available NUMA nodes
  - @return NUMA node number, or -1 if no specific assignment
    */
    [[nodiscard]] int get_numa_node(int worker_id, size_t total_workers,
    const std::vector<int>& available_nodes) const {
    if (!enabled || available_nodes.empty()) {
    return -1;
    }
    
    // Explicit assignment takes precedence
    auto it = node_assignments.find(worker_id);
    if (it != node_assignments.end()) {
    return it->second;
    }
    
    // Automatic assignment: distribute workers evenly across nodes
    if (balance_workers) {
    return available_nodes[worker_id % available_nodes.size()];
    }
    
    return available_nodes[0]; // Use first available node
    }
  
  /**
  - @brief Validate NUMA configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    // Check node assignments are non-negative
    for (const auto& [worker_id, node] : node_assignments) {
    if (worker_id < 0 || node < 0) {
    return false;
    }
    }
    
    return min_workers_per_node > 0;
    }
    };

// ============================================================================
// Thread Pool Configuration
// ============================================================================

/**

- @brief Thread pool configuration for worker management
  */
  struct ThreadPoolConfig {
  /**
  - @brief Thread naming convention
    */
    struct Naming {
    std::string prefix = “stellane-worker”;     ///< Thread name prefix
    bool include_worker_id = true;             ///< Include worker ID in name
    bool include_pid = false;                  ///< Include process ID in name
    } naming;
  
  /**
  - @brief Thread priority settings
    */
    struct Priority {
    /**
    - @brief Thread priority level
      */
      enum class Level {
      NORMAL = 0,     ///< Normal priority
      HIGH = 1,       ///< High priority (requires privileges on some systems)
      REALTIME = 2    ///< Real-time priority (requires privileges)
      };
    
    Level level = Level::NORMAL;               ///< Priority level for worker threads
    int nice_value = 0;                       ///< Nice value (Unix systems)
    bool use_realtime = false;                ///< Enable real-time scheduling
    int realtime_priority = 1;                ///< Real-time priority (1-99)
    } priority;
  
  /**
  - @brief Stack size configuration
    */
    struct StackSize {
    std::optional<size_t> size_bytes;          ///< Custom stack size (bytes)
    bool use_system_default = true;           ///< Use system default stack size
    size_t guard_page_size = 4096;            ///< Guard page size for overflow detection
    } stack;
  
  /**
  - @brief Thread lifecycle callbacks
    */
    struct Callbacks {
    std::function<void(int)> on_worker_start;  ///< Called when worker starts
    std::function<void(int)> on_worker_stop;   ///< Called when worker stops
    std::function<void(int, const std::exception&)> on_worker_error; ///< Called on worker error
    } callbacks;
    };

// ============================================================================
// Main Worker Configuration
// ============================================================================

/**

- @brief Comprehensive worker thread configuration
- 
- Combines all worker-related settings including thread management,
- CPU affinity, work-stealing, and NUMA optimizations.
  */
  struct WorkerConfig {
  // ========================================================================
  // Basic Worker Settings
  // ========================================================================
  
  /**
  - @brief Number of worker threads
  - 
  - Default is hardware_concurrency(), but can be adjusted based on workload.
  - For CPU-intensive: use physical cores
  - For I/O-intensive: can exceed physical cores
    */
    size_t worker_threads = std::thread::hardware_concurrency();
  
  /**
  - @brief Maximum tasks processed per event loop iteration
  - 
  - Higher values increase throughput but may increase latency.
  - Lower values reduce latency but may decrease throughput.
    */
    size_t max_tasks_per_loop = 1000;
  
  /**
  - @brief Worker thread idle timeout
  - 
  - How long workers wait for new tasks before checking for shutdown.
    */
    std::chrono::milliseconds idle_timeout{100};
  
  // ========================================================================
  // Advanced Configuration
  // ========================================================================
  
  AffinityConfig affinity;                       ///< CPU affinity configuration
  WorkStealingConfig work_stealing;              ///< Work-stealing configuration
  NumaConfig numa;                               ///< NUMA optimization configuration
  ThreadPoolConfig thread_pool;                 ///< Thread pool management
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Get effective number of worker threads
  - 
  - Adjusts worker count based on system capabilities and configuration.
  - @return Effective worker thread count
    */
    [[nodiscard]] size_t get_effective_worker_count() const {
    size_t hardware_cores = std::thread::hardware_concurrency();
    
    // Ensure we have at least 1 worker
    size_t effective_count = std::max(1UL, worker_threads);
    
    // Cap at reasonable maximum (4x hardware cores)
    effective_count = std::min(effective_count, hardware_cores * 4);
    
    // Account for main thread isolation
    if (affinity.isolate_main_thread && effective_count == hardware_cores) {
    effective_count = std::max(1UL, effective_count - 1);
    }
    
    return effective_count;
    }
  
  /**
  - @brief Estimate memory usage for worker configuration
  - @return Estimated memory usage in bytes
    */
    [[nodiscard]] size_t estimated_memory_usage() const {
    size_t total_memory = 0;
    
    size_t effective_workers = get_effective_worker_count();
    
    // Stack memory per worker
    size_t stack_size = thread_pool.stack.use_system_default ?
    (2 * 1024 * 1024) : // 2MB default
    thread_pool.stack.size_bytes.value_or(2 * 1024 * 1024);
    
    total_memory += effective_workers * stack_size;
    
    // Work-stealing queue overhead
    if (work_stealing.enabled) {
    total_memory += effective_workers * 64 * 1024; // 64KB per worker queue
    }
    
    // NUMA optimization overhead
    if (numa.enabled) {
    total_memory += effective_workers * 4 * 1024; // 4KB per worker for NUMA metadata
    }
    
    return total_memory;
    }
  
  /**
  - @brief Validate worker configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    if (worker_threads == 0 || max_tasks_per_loop == 0) {
    return false;
    }
    
    if (idle_timeout.count() < 0) {
    return false;
    }
    
    // Validate sub-configurations
    return affinity.is_valid() &&
    work_stealing.is_valid() &&
    numa.is_valid();
    }
  
  /**
  - @brief Auto-configure for specific workload types
  - @param cpu_intensive Whether workload is CPU-intensive
  - @param io_intensive Whether workload is I/O-intensive
    */
    void auto_configure_for_workload(bool cpu_intensive, bool io_intensive) {
    size_t hardware_cores = std::thread::hardware_concurrency();
    
    if (cpu_intensive && !io_intensive) {
    // CPU-bound: use physical cores, enable affinity
    worker_threads = hardware_cores;
    affinity.strategy = AffinityStrategy::ROUND_ROBIN;
    work_stealing.policy = WorkStealingPolicy::CONSERVATIVE;
    max_tasks_per_loop = 100; // Lower latency
    
    } else if (io_intensive && !cpu_intensive) {
    // I/O-bound: can use more threads than cores
    worker_threads = hardware_cores * 2;
    affinity.strategy = AffinityStrategy::NONE;
    work_stealing.policy = WorkStealingPolicy::AGGRESSIVE;
    max_tasks_per_loop = 2000; // Higher throughput
    
    } else if (cpu_intensive && io_intensive) {
    // Mixed workload: balanced approach
    worker_threads = hardware_cores + (hardware_cores / 2);
    affinity.strategy = AffinityStrategy::NUMA_AWARE;
    work_stealing.policy = WorkStealingPolicy::ADAPTIVE;
    max_tasks_per_loop = 1000; // Balanced
    
    } else {
    // General purpose: conservative settings
    worker_threads = hardware_cores;
    affinity.strategy = AffinityStrategy::NONE;
    work_stealing.policy = WorkStealingPolicy::CONSERVATIVE;
    max_tasks_per_loop = 500; // Conservative
    }
    
    // Enable NUMA on multi-socket systems
    if (hardware_cores >= 16) {
    numa.enabled = true;
    numa.balance_workers = true;
    }
    }
    };

} // namespace stellane
