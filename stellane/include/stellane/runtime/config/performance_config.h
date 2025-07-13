#pragma once

#include “stellane/runtime/config/config_enums.h”
#include <chrono>
#include <optional>
#include <vector>
#include <string>
#include <cstddef>

namespace stellane {

// ============================================================================
// Memory Performance Configuration
// ============================================================================

/**

- @brief Memory management and optimization settings
- 
- Controls memory allocation patterns, garbage collection,
- and memory-related performance optimizations.
  */
  struct MemoryConfig {
  /**
  - @brief Memory allocation strategy
    */
    enum class AllocationStrategy {
    SYSTEM_DEFAULT = 0,     ///< Use system malloc/free
    POOLED = 1,            ///< Use memory pools for common sizes
    ARENA = 2,             ///< Use arena allocators
    TCMALLOC = 3,          ///< Use Google’s TCMalloc
    JEMALLOC = 4,          ///< Use Facebook’s jemalloc
    MIMALLOC = 5           ///< Use Microsoft’s mimalloc
    };
  
  AllocationStrategy strategy = AllocationStrategy::SYSTEM_DEFAULT;
  
  /**
  - @brief Enable memory pooling for frequent allocations
    */
    bool enable_pooling = true;
  
  /**
  - @brief Pool sizes for common allocation patterns
    */
    struct PoolSizes {
    size_t small_object_size = 256;        ///< Size for small object pool
    size_t medium_object_size = 4096;      ///< Size for medium object pool
    size_t large_object_size = 65536;      ///< Size for large object pool
    
    size_t small_pool_count = 1024;        ///< Number of small objects to pool
    size_t medium_pool_count = 256;        ///< Number of medium objects to pool
    size_t large_pool_count = 64;          ///< Number of large objects to pool
    } pools;
  
  /**
  - @brief Garbage collection settings
    */
    struct GarbageCollection {
    bool enabled = false;                  ///< Enable periodic cleanup
    std::chrono::milliseconds interval{5000}; ///< GC interval
    double trigger_threshold = 0.8;        ///< Memory usage threshold to trigger GC
    size_t max_heap_size_mb = 0;          ///< Max heap size (0 = unlimited)
    } gc;
  
  /**
  - @brief Memory alignment and padding
    */
    struct Alignment {
    size_t cache_line_size = 64;          ///< CPU cache line size for alignment
    bool align_allocations = true;        ///< Align allocations to cache lines
    bool pad_hot_data = true;             ///< Pad frequently accessed data
    } alignment;
  
  /**
  - @brief Memory prefetching hints
    */
    struct Prefetch {
    bool enable_prefetching = true;       ///< Enable software prefetching
    size_t prefetch_distance = 2;         ///< Cache lines to prefetch ahead
    bool prefetch_for_write = false;      ///< Prefetch with write intent
    } prefetch;
  
  /**
  - @brief Validate memory configuration
    */
    [[nodiscard]] bool is_valid() const {
    return pools.small_object_size > 0 &&
    pools.medium_object_size > pools.small_object_size &&
    pools.large_object_size > pools.medium_object_size &&
    pools.small_pool_count > 0 &&
    pools.medium_pool_count > 0 &&
    pools.large_pool_count > 0 &&
    gc.trigger_threshold >= 0.0 && gc.trigger_threshold <= 1.0 &&
    alignment.cache_line_size > 0 &&
    prefetch.prefetch_distance > 0;
    }
    };

// ============================================================================
// Network Performance Configuration
// ============================================================================

/**

- @brief Network stack optimization settings
- 
- Controls TCP/UDP parameters, socket options,
- and network-related performance tuning.
  */
  struct NetworkConfig {
  /**
  - @brief TCP socket optimization
    */
    struct TcpConfig {
    bool enable_nodelay = true;            ///< Disable Nagle algorithm (TCP_NODELAY)
    bool enable_quickack = true;           ///< Enable TCP quick ACK
    bool enable_cork = false;              ///< Enable TCP_CORK for batching
    bool enable_keepalive = true;          ///< Enable TCP keepalive
    
    // Buffer sizes
    std::optional<size_t> send_buffer_size;    ///< SO_SNDBUF size (bytes)
    std::optional<size_t> receive_buffer_size; ///< SO_RCVBUF size (bytes)
    
    // Keepalive parameters
    std::chrono::seconds keepalive_time{7200};     ///< Time before keepalive probes
    std::chrono::seconds keepalive_interval{75};   ///< Interval between probes
    int keepalive_probes = 9;                      ///< Number of probes before timeout
    
    // Congestion control
    std::string congestion_algorithm = “cubic”;    ///< TCP congestion algorithm
    bool enable_timestamps = true;                 ///< Enable TCP timestamps
    bool enable_window_scaling = true;             ///< Enable TCP window scaling
    } tcp;
  
  /**
  - @brief UDP socket optimization
    */
    struct UdpConfig {
    std::optional<size_t> send_buffer_size;    ///< SO_SNDBUF size (bytes)
    std::optional<size_t> receive_buffer_size; ///< SO_RCVBUF size (bytes)
    bool enable_broadcast = false;             ///< Enable broadcast
    bool enable_multicast = false;             ///< Enable multicast
    int multicast_ttl = 1;                     ///< Multicast TTL
    } udp;
  
  /**
  - @brief Connection management
    */
    struct ConnectionConfig {
    size_t max_connections = 100000;           ///< Maximum concurrent connections
    size_t connection_backlog = 1024;          ///< Listen backlog size
    std::chrono::seconds connection_timeout{30}; ///< Connection timeout
    bool enable_reuseaddr = true;              ///< Enable SO_REUSEADDR
    bool enable_reuseport = true;              ///< Enable SO_REUSEPORT
    
    // Connection pooling
    bool enable_connection_pooling = true;     ///< Enable connection reuse
    size_t max_idle_connections = 1000;        ///< Max idle connections to keep
    std::chrono::seconds idle_timeout{300};    ///< Idle connection timeout
    } connection;
  
  /**
  - @brief Zero-copy optimizations
    */
    struct ZeroCopyConfig {
    bool enable_sendfile = true;               ///< Use sendfile() for file transfers
    bool enable_splice = true;                 ///< Use splice() for pipe transfers
    bool enable_mmap = true;                   ///< Use mmap for large files
    size_t mmap_threshold = 16384;             ///< Minimum size for mmap (bytes)
    bool enable_gather_writes = true;          ///< Use writev() for scatter-gather
    } zero_copy;
  
  /**
  - @brief Validate network configuration
    */
    [[nodiscard]] bool is_valid() const {
    return tcp.keepalive_time.count() > 0 &&
    tcp.keepalive_interval.count() > 0 &&
    tcp.keepalive_probes > 0 &&
    udp.multicast_ttl > 0 &&
    connection.max_connections > 0 &&
    connection.connection_backlog > 0 &&
    connection.connection_timeout.count() > 0 &&
    connection.max_idle_connections > 0 &&
    connection.idle_timeout.count() > 0 &&
    zero_copy.mmap_threshold > 0;
    }
    };

// ============================================================================
// I/O Performance Configuration
// ============================================================================

/**

- @brief I/O subsystem performance settings
- 
- Controls batching, buffering, and I/O-related optimizations
- for maximum throughput and minimal latency.
  */
  struct IoConfig {
  /**
  - @brief I/O batching strategy
    */
    enum class BatchingStrategy {
    NONE = 0,           ///< No batching (immediate I/O)
    SIZE_BASED = 1,     ///< Batch based on data size
    TIME_BASED = 2,     ///< Batch based on time window
    ADAPTIVE = 3,       ///< Adaptive batching based on load
    HYBRID = 4          ///< Combination of size and time
    };
  
  BatchingStrategy batching_strategy = BatchingStrategy::ADAPTIVE;
  
  /**
  - @brief Enable zero-copy I/O operations
    */
    bool zero_copy_io = true;
  
  /**
  - @brief I/O batch size (number of operations)
    */
    int io_batch_size = 64;
  
  /**
  - @brief Maximum time to wait for batch completion
    */
    std::chrono::microseconds batch_timeout{100};
  
  /**
  - @brief I/O queue depth for asynchronous operations
    */
    size_t queue_depth = 256;
  
  /**
  - @brief Direct I/O settings
    */
    struct DirectIo {
    bool enable_direct_io = false;         ///< Enable O_DIRECT for file I/O
    size_t alignment_requirement = 4096;   ///< Alignment requirement for direct I/O
    bool bypass_page_cache = false;        ///< Bypass kernel page cache
    } direct_io;
  
  /**
  - @brief Read-ahead and prefetching
    */
    struct ReadAhead {
    bool enable_readahead = true;          ///< Enable read-ahead
    size_t readahead_size = 131072;        ///< Read-ahead size (128KB)
    bool sequential_detection = true;      ///< Detect sequential access patterns
    double random_threshold = 0.3;         ///< Threshold for random vs sequential
    } readahead;
  
  /**
  - @brief Write optimization
    */
    struct WriteOptimization {
    bool enable_write_combining = true;    ///< Combine small writes
    size_t write_combine_threshold = 4096; ///< Minimum size for combining
    bool enable_write_behind = true;       ///< Enable asynchronous writes
    size_t writeback_threshold = 65536;    ///< Threshold for write-behind
    } write_optimization;
  
  /**
  - @brief Get effective batch size based on current load
  - @param current_load Current system load (0.0 - 1.0)
  - @return Effective batch size
    */
    [[nodiscard]] int get_effective_batch_size(double current_load = 0.5) const {
    switch (batching_strategy) {
    case BatchingStrategy::NONE:
    return 1;
    
    ```
     case BatchingStrategy::SIZE_BASED:
         return io_batch_size;
         
     case BatchingStrategy::TIME_BASED:
         return io_batch_size;
         
     case BatchingStrategy::ADAPTIVE:
         if (current_load > 0.8) {
             return io_batch_size * 2; // Increase batching under high load
         } else if (current_load < 0.2) {
             return std::max(1, io_batch_size / 2); // Reduce batching under low load
         }
         return io_batch_size;
         
     case BatchingStrategy::HYBRID:
         return static_cast<int>(io_batch_size * (0.5 + current_load * 0.5));
         
     default:
         return io_batch_size;
    ```
    
    }
    }
  
  /**
  - @brief Validate I/O configuration
    */
    [[nodiscard]] bool is_valid() const {
    return io_batch_size > 0 &&
    batch_timeout.count() > 0 &&
    queue_depth > 0 &&
    direct_io.alignment_requirement > 0 &&
    readahead.readahead_size > 0 &&
    readahead.random_threshold >= 0.0 && readahead.random_threshold <= 1.0 &&
    write_optimization.write_combine_threshold > 0 &&
    write_optimization.writeback_threshold > 0;
    }
    };

// ============================================================================
// Profiling and Monitoring Configuration
// ============================================================================

/**

- @brief Performance profiling and monitoring settings
- 
- Controls collection of performance metrics, profiling data,
- and monitoring overhead vs. detail trade-offs.
  */
  struct ProfilingConfig {
  /**
  - @brief Overall profiling level
    */
    enum class ProfilingLevel {
    DISABLED = 0,       ///< No profiling (production)
    BASIC = 1,          ///< Basic counters only
    DETAILED = 2,       ///< Detailed timing and metrics
    VERBOSE = 3,        ///< Verbose profiling with traces
    DEBUG = 4           ///< Full debug profiling
    };
  
  ProfilingLevel level = ProfilingLevel::BASIC;
  
  /**
  - @brief Enable specific profiling categories
    */
    struct Categories {
    bool task_execution = true;            ///< Profile task execution times
    bool memory_allocation = false;        ///< Profile memory allocations
    bool io_operations = true;             ///< Profile I/O operations
    bool network_activity = true;          ///< Profile network operations
    bool lock_contention = false;          ///< Profile lock contention
    bool cache_performance = false;        ///< Profile cache hit/miss rates
    } categories;
  
  /**
  - @brief Sampling configuration
    */
    struct Sampling {
    bool enable_sampling = true;           ///< Use sampling instead of full profiling
    double sampling_rate = 0.01;           ///< Sampling rate (1% by default)
    std::chrono::milliseconds interval{100}; ///< Sampling interval
    size_t max_samples = 10000;            ///< Maximum samples to keep
    } sampling;
  
  /**
  - @brief Performance counter collection
    */
    struct Counters {
    bool enable_cpu_counters = true;       ///< Collect CPU performance counters
    bool enable_memory_counters = true;    ///< Collect memory statistics
    bool enable_io_counters = true;        ///< Collect I/O statistics
    bool enable_network_counters = true;   ///< Collect network statistics
    
    // Hardware performance counters (requires perf_events on Linux)
    bool enable_hardware_counters = false; ///< Enable hardware PMU counters
    std::vector<std::string> pmu_events;   ///< Specific PMU events to collect
    } counters;
  
  /**
  - @brief Trace collection
    */
    struct Tracing {
    bool enable_tracing = false;           ///< Enable execution tracing
    size_t trace_buffer_size = 1048576;    ///< Trace buffer size (1MB)
    bool trace_syscalls = false;           ///< Trace system calls
    bool trace_function_calls = false;     ///< Trace function calls
    std::chrono::milliseconds flush_interval{1000}; ///< Trace flush interval
    } tracing;
  
  /**
  - @brief Export and reporting
    */
    struct Export {
    bool enable_export = true;             ///< Enable metric export
    
    enum class Format {
    JSON = 0,           ///< JSON format
    PROMETHEUS = 1,     ///< Prometheus format
    CSV = 2,            ///< CSV format
    BINARY = 3          ///< Binary format
    };
    
    Format format = Format::JSON;
    std::chrono::seconds export_interval{60}; ///< Export interval
    std::string export_path = “/tmp/stellane_metrics”; ///< Export file path
    bool compress_exports = true;          ///< Compress exported data
    } export_config;
  
  /**
  - @brief Get profiling overhead estimate
  - @return Estimated overhead percentage (0.0 - 1.0)
    */
    [[nodiscard]] double get_overhead_estimate() const {
    double overhead = 0.0;
    
    switch (level) {
    case ProfilingLevel::DISABLED: overhead = 0.0; break;
    case ProfilingLevel::BASIC: overhead = 0.01; break;      // 1%
    case ProfilingLevel::DETAILED: overhead = 0.05; break;   // 5%
    case ProfilingLevel::VERBOSE: overhead = 0.10; break;    // 10%
    case ProfilingLevel::DEBUG: overhead = 0.20; break;      // 20%
    }
    
    // Adjust for sampling
    if (sampling.enable_sampling) {
    overhead *= sampling.sampling_rate;
    }
    
    // Additional overhead for specific features
    if (categories.memory_allocation) overhead += 0.02;
    if (categories.lock_contention) overhead += 0.03;
    if (categories.cache_performance) overhead += 0.01;
    if (counters.enable_hardware_counters) overhead += 0.01;
    if (tracing.enable_tracing) overhead += 0.05;
    
    return std::min(overhead, 0.5); // Cap at 50%
    }
  
  /**
  - @brief Validate profiling configuration
    */
    [[nodiscard]] bool is_valid() const {
    return sampling.sampling_rate >= 0.0 && sampling.sampling_rate <= 1.0 &&
    sampling.interval.count() > 0 &&
    sampling.max_samples > 0 &&
    tracing.trace_buffer_size > 0 &&
    tracing.flush_interval.count() > 0 &&
    export_config.export_interval.count() > 0;
    }
    };

// ============================================================================
// Main Performance Configuration
// ============================================================================

/**

- @brief Comprehensive performance configuration
- 
- Combines all performance-related settings including memory management,
- network optimization, I/O tuning, and profiling controls.
  */
  struct PerformanceConfig {
  // ========================================================================
  // Basic Performance Settings
  // ========================================================================
  
  /**
  - @brief Enable CPU affinity for threads
    */
    bool enable_cpu_affinity = false;
  
  /**
  - @brief Enable NUMA-aware optimizations
    */
    bool numa_aware = false;
  
  /**
  - @brief Event loop idle timeout
  - 
  - How long to wait for events before checking for shutdown or other work.
  - Lower values reduce latency but increase CPU usage.
    */
    std::chrono::milliseconds idle_timeout{100};
  
  /**
  - @brief Enable performance profiling
    */
    bool enable_profiling = false;
  
  // ========================================================================
  // Sub-configuration Components
  // ========================================================================
  
  MemoryConfig memory;                       ///< Memory management settings
  NetworkConfig network;                     ///< Network optimization settings
  IoConfig io;                              ///< I/O performance settings
  ProfilingConfig profiling;                ///< Profiling and monitoring settings
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Estimate total performance overhead
  - @return Estimated overhead percentage (0.0 - 1.0)
    */
    [[nodiscard]] double estimate_overhead() const {
    double total_overhead = 0.0;
    
    // Profiling overhead
    if (enable_profiling) {
    total_overhead += profiling.get_overhead_estimate();
    }
    
    // Memory management overhead
    if (memory.enable_pooling) {
    total_overhead += 0.005; // 0.5% for memory pooling
    }
    
    if (memory.gc.enabled) {
    total_overhead += 0.01; // 1% for garbage collection
    }
    
    // I/O batching overhead
    if (io.batching_strategy != IoConfig::BatchingStrategy::NONE) {
    total_overhead += 0.002; // 0.2% for I/O batching
    }
    
    // CPU affinity overhead
    if (enable_cpu_affinity) {
    total_overhead += 0.001; // 0.1% for affinity management
    }
    
    return std::min(total_overhead, 0.3); // Cap at 30%
    }
  
  /**
  - @brief Estimate memory usage for performance features
  - @return Estimated memory usage in bytes
    */
    [[nodiscard]] size_t estimate_memory_usage() const {
    size_t total_memory = 0;
    
    // Memory pools
    if (memory.enable_pooling) {
    total_memory += memory.pools.small_pool_count * memory.pools.small_object_size;
    total_memory += memory.pools.medium_pool_count * memory.pools.medium_object_size;
    total_memory += memory.pools.large_pool_count * memory.pools.large_object_size;
    }
    
    // Profiling buffers
    if (enable_profiling) {
    if (profiling.sampling.enable_sampling) {
    total_memory += profiling.sampling.max_samples * 256; // 256 bytes per sample
    }
    
    ```
     if (profiling.tracing.enable_tracing) {
         total_memory += profiling.tracing.trace_buffer_size;
     }
    ```
    
    }
    
    // I/O buffers
    total_memory += io.queue_depth * 4096; // 4KB per I/O operation
    
    return total_memory;
    }
  
  /**
  - @brief Validate entire performance configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    return idle_timeout.count() >= 0 &&
    memory.is_valid() &&
    network.is_valid() &&
    io.is_valid() &&
    profiling.is_valid();
    }
  
  /**
  - @brief Auto-tune for specific performance goals
  - @param target_latency Target latency in milliseconds
  - @param target_throughput Target throughput (operations per second)
    */
    void auto_tune_for_goals(std::chrono::milliseconds target_latency,
    size_t target_throughput) {
    // Tune for low latency
    if (target_latency < std::chrono::milliseconds(10)) {
    idle_timeout = std::chrono::milliseconds(1);
    io.batching_strategy = IoConfig::BatchingStrategy::NONE;
    io.io_batch_size = 1;
    enable_cpu_affinity = true;
    profiling.level = ProfilingConfig::ProfilingLevel::BASIC;
    }
    // Tune for high throughput  
    else if (target_throughput > 100000) {
    io.batching_strategy = IoConfig::BatchingStrategy::ADAPTIVE;
    io.io_batch_size = 128;
    memory.enable_pooling = true;
    network.tcp.enable_cork = true;
    io.zero_copy_io = true;
    }
    // Balanced configuration
    else {
    idle_timeout = std::chrono::milliseconds(10);
    io.batching_strategy = IoConfig::BatchingStrategy::HYBRID;
    io.io_batch_size = 32;
    memory.enable_pooling = true;
    enable_cpu_affinity = false;
    }
    }
  
  /**
  - @brief Create configuration optimized for real-time applications
  - @return Performance configuration for real-time workloads
    */
    static PerformanceConfig create_realtime_config() {
    PerformanceConfig config;
    
    // Minimize latency
    config.idle_timeout = std::chrono::microseconds(100);
    config.enable_cpu_affinity = true;
    config.numa_aware = true;
    
    // Optimize I/O for low latency
    config.io.batching_strategy = IoConfig::BatchingStrategy::NONE;
    config.io.io_batch_size = 1;
    config.io.zero_copy_io = true;
    
    // Optimize network for low latency
    config.network.tcp.enable_nodelay = true;
    config.network.tcp.enable_quickack = true;
    config.network.tcp.enable_cork = false;
    
    // Minimize profiling overhead
    config.enable_profiling = false;
    config.profiling.level = ProfilingConfig::ProfilingLevel::DISABLED;
    
    // Optimize memory for predictable allocation
    config.memory.enable_pooling = true;
    config.memory.strategy = MemoryConfig::AllocationStrategy::POOLED;
    config.memory.gc.enabled = false;
    
    return config;
    }
  
  /**
  - @brief Create configuration optimized for high throughput
  - @return Performance configuration for throughput workloads
    */
    static PerformanceConfig create_throughput_config() {
    PerformanceConfig config;
    
    // Optimize for throughput
    config.idle_timeout = std::chrono::milliseconds(10);
    config.enable_cpu_affinity = false;
    config.numa_aware = true;
    
    // Aggressive I/O batching
    config.io.batching_strategy = IoConfig::BatchingStrategy::ADAPTIVE;
    config.io.io_batch_size = 256;
    config.io.zero_copy_io = true;
    
    // Optimize network for throughput
    config.network.tcp.enable_nodelay = false;
    config.network.tcp.enable_cork = true;
    config.network.zero_copy.enable_sendfile = true;
    
    // Enable profiling for optimization
    config.enable_profiling = true;
    config.profiling.level = ProfilingConfig::ProfilingLevel::BASIC;
    config.profiling.sampling.enable_sampling = true;
    
    // Optimize memory for high allocation rates
    config.memory.enable_pooling = true;
    config.memory.strategy = MemoryConfig::AllocationStrategy::TCMALLOC;
    config.memory.gc.enabled = true;
    
    return config;
    }
    };

} // namespace stellane
