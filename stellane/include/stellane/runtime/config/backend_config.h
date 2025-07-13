#pragma once

#include “stellane/runtime/config/config_enums.h”
#include <functional>
#include <memory>
#include <string>
#include <chrono>
#include <optional>

namespace stellane {

// Forward declarations
class IEventLoopBackend;

// ============================================================================
// Backend-Specific Configuration Structures
// ============================================================================

/**

- @brief LibUV backend configuration
- 
- Cross-platform event loop with balanced performance characteristics.
- Good default choice for most applications.
  */
  struct LibUVConfig {
  /**
  - @brief UV loop configuration mode
    */
    enum class LoopMode {
    DEFAULT = 0,    ///< Default UV_RUN_DEFAULT mode
    ONCE = 1,       ///< UV_RUN_ONCE mode
    NOWAIT = 2      ///< UV_RUN_NOWAIT mode
    };
  
  LoopMode loop_mode = LoopMode::DEFAULT;     ///< UV loop run mode
  size_t thread_pool_size = 4;               ///< UV thread pool size for file I/O
  bool use_signal_handling = true;           ///< Enable UV signal handling
  size_t max_connections = 65536;            ///< Maximum concurrent connections
  std::chrono::milliseconds keepalive_timeout{60000}; ///< TCP keepalive timeout
  
  // Advanced options
  bool enable_idle_handles = false;          ///< Enable UV idle handles for low-priority work
  size_t write_buffer_size = 65536;          ///< Default write buffer size
  size_t read_buffer_size = 65536;           ///< Default read buffer size
  };

/**

- @brief Linux epoll backend configuration
- 
- High-performance single-threaded event loop for Linux.
- Optimized for maximum throughput on single-core performance.
  */
  struct EpollConfig {
  /**
  - @brief Epoll behavior flags
    */
    enum class EpollFlags {
    NONE = 0,
    CLOEXEC = 1 << 0,   ///< Set FD_CLOEXEC flag
    NONBLOCK = 1 << 1   ///< Set O_NONBLOCK flag
    };
  
  size_t max_events = 1024;                  ///< Maximum events per epoll_wait call
  std::chrono::milliseconds poll_timeout{10}; ///< Epoll timeout
  EpollFlags flags = EpollFlags::CLOEXEC;     ///< Epoll creation flags
  
  // Edge-triggered vs Level-triggered
  bool use_edge_triggered = true;            ///< Use EPOLLET (edge-triggered)
  bool use_oneshot = false;                  ///< Use EPOLLONESHOT
  
  // Performance tuning
  size_t initial_fd_capacity = 1024;         ///< Initial file descriptor capacity
  bool enable_reuseport = true;              ///< Enable SO_REUSEPORT for load balancing
  int tcp_nodelay = 1;                       ///< TCP_NODELAY setting
  int tcp_quickack = 1;                      ///< TCP_QUICKACK setting
  };

/**

- @brief Linux io_uring backend configuration
- 
- Ultra-high performance asynchronous I/O for Linux 5.1+.
- Best choice for maximum throughput and minimal latency.
  */
  struct IoUringConfig {
  /**
  - @brief io_uring setup flags
    */
    enum class SetupFlags : uint32_t {
    NONE = 0,
    IOPOLL = 1 << 0,        ///< Use polling mode for I/O
    SQPOLL = 1 << 1,        ///< Use kernel polling thread for SQ
    SQ_AFF = 1 << 2,        ///< Set SQ polling thread CPU affinity
    CQSIZE = 1 << 3,        ///< Custom CQ size
    CLAMP = 1 << 4,         ///< Clamp SQ/CQ sizes to supported values
    ATTACH_WQ = 1 << 5      ///< Attach to existing work queue
    };
  
  size_t queue_depth = 256;                  ///< Submission/completion queue depth
  SetupFlags setup_flags = SetupFlags::NONE; ///< io_uring setup flags
  
  // SQ polling configuration
  uint32_t sq_thread_cpu = 0;                ///< CPU for SQ polling thread
  uint32_t sq_thread_idle = 2000;            ///< SQ thread idle timeout (ms)
  
  // Batching and performance
  size_t submit_batch_size = 32;             ///< Batch size for submissions
  size_t completion_batch_size = 64;         ///< Batch size for completions
  bool use_zero_copy = true;                 ///< Enable zero-copy operations
  bool use_fixed_buffers = false;            ///< Use pre-registered fixed buffers
  size_t fixed_buffer_size = 4096;           ///< Size of each fixed buffer
  size_t fixed_buffer_count = 1024;          ///< Number of fixed buffers
  
  // Advanced features
  bool enable_multishot = true;              ///< Enable multishot operations
  bool enable_buffer_ring = false;           ///< Use buffer ring for receives
  size_t buffer_ring_size = 512;             ///< Buffer ring size
  };

/**

- @brief Custom Stellane backend configuration
- 
- Multi-threaded custom implementation optimized for Stellane workloads.
- Provides balanced performance across different scenarios.
  */
  struct StellaneConfig {
  /**
  - @brief Load balancing strategies for multi-loop
    */
    enum class LoadBalancing {
    ROUND_ROBIN = 0,    ///< Simple round-robin distribution
    LEAST_CONNECTIONS = 1, ///< Route to loop with fewest connections
    WEIGHTED = 2,       ///< Weighted distribution based on CPU usage
    HASH_BASED = 3,     ///< Hash-based consistent routing
    ADAPTIVE = 4        ///< Adaptive based on real-time metrics
    };
  
  size_t io_threads = 2;                     ///< Number of I/O threads
  LoadBalancing load_balancing = LoadBalancing::LEAST_CONNECTIONS;
  
  // Connection management
  size_t max_connections_per_thread = 10000; ///< Max connections per I/O thread
  std::chrono::milliseconds connection_timeout{30000}; ///< Connection timeout
  size_t backlog_size = 1024;               ///< Listen backlog size
  
  // Buffer management
  size_t buffer_pool_size = 1024;           ///< Number of buffers in pool
  size_t buffer_size = 16384;               ///< Size of each buffer (16KB)
  bool use_buffer_recycling = true;         ///< Enable buffer recycling
  
  // Performance optimizations
  bool enable_nagle_algorithm = false;      ///< Disable Nagle algorithm (TCP_NODELAY)
  bool enable_cork = false;                 ///< Enable TCP_CORK for batching
  size_t send_buffer_size = 262144;         ///< SO_SNDBUF size (256KB)
  size_t receive_buffer_size = 262144;      ///< SO_RCVBUF size (256KB)
  };

// ============================================================================
// Main Backend Configuration
// ============================================================================

/**

- @brief Event loop backend configuration
- 
- Contains the selected backend type and all backend-specific configurations.
- Only the configuration for the selected backend is used at runtime.
  */
  struct BackendConfig {
  // ========================================================================
  // Backend Selection
  // ========================================================================
  
  RuntimeBackend type = RuntimeBackend::LIBUV;    ///< Selected backend type
  
  // ========================================================================
  // Backend-Specific Configurations
  // ========================================================================
  
  LibUVConfig libuv;                              ///< LibUV configuration
  EpollConfig epoll;                              ///< Epoll configuration  
  IoUringConfig io_uring;                         ///< io_uring configuration
  StellaneConfig stellane;                        ///< Stellane custom configuration
  
  // ========================================================================
  // Custom Backend Support
  // ========================================================================
  
  /**
  - @brief Custom backend factory function
  - 
  - Used when type == RuntimeBackend::CUSTOM.
  - Must return a valid IEventLoopBackend implementation.
    */
    std::function<std::unique_ptr<IEventLoopBackend>()> custom_factory;
  
  /**
  - @brief Custom backend name for logging/debugging
    */
    std::string custom_backend_name = “custom”;
  
  // ========================================================================
  // Cross-Backend Settings
  // ========================================================================
  
  /**
  - @brief Maximum number of concurrent connections across all backends
    */
    size_t global_max_connections = 100000;
  
  /**
  - @brief Enable backend-specific optimizations
    */
    bool enable_optimizations = true;
  
  /**
  - @brief Fallback backend if primary fails to initialize
    */
    std::optional<RuntimeBackend> fallback_backend;
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Get the name of the current backend
  - @return Backend name as string
    */
    [[nodiscard]] std::string get_backend_name() const {
    switch (type) {
    case RuntimeBackend::LIBUV: return “libuv”;
    case RuntimeBackend::EPOLL: return “epoll”;
    case RuntimeBackend::IO_URING: return “io_uring”;
    case RuntimeBackend::STELLANE: return “stellane”;
    case RuntimeBackend::CUSTOM: return custom_backend_name;
    default: return “unknown”;
    }
    }
  
  /**
  - @brief Check if backend supports multiple I/O threads
  - @return true if backend is multi-threaded
    */
    [[nodiscard]] bool is_multithreaded() const {
    return type == RuntimeBackend::STELLANE ||
    type == RuntimeBackend::CUSTOM;
    }
  
  /**
  - @brief Get estimated memory usage for this backend configuration
  - @return Estimated memory usage in bytes
    */
    [[nodiscard]] size_t estimated_memory_usage() const {
    size_t base_memory = 1024 * 1024; // 1MB base
    
    switch (type) {
    case RuntimeBackend::LIBUV:
    base_memory += libuv.max_connections * 256; // ~256 bytes per connection
    base_memory += libuv.thread_pool_size * 2 * 1024 * 1024; // 2MB per thread
    break;
    
    ```
     case RuntimeBackend::EPOLL:
         base_memory += epoll.max_events * sizeof(void*) * 2; // Event structure overhead
         base_memory += epoll.initial_fd_capacity * 64; // ~64 bytes per FD
         break;
         
     case RuntimeBackend::IO_URING:
         base_memory += io_uring.queue_depth * 128; // SQE + CQE overhead
         if (io_uring.use_fixed_buffers) {
             base_memory += io_uring.fixed_buffer_count * io_uring.fixed_buffer_size;
         }
         break;
         
     case RuntimeBackend::STELLANE:
         base_memory += stellane.io_threads * 4 * 1024 * 1024; // 4MB per I/O thread
         base_memory += stellane.buffer_pool_size * stellane.buffer_size;
         break;
         
     case RuntimeBackend::CUSTOM:
         base_memory += 10 * 1024 * 1024; // 10MB estimate for custom
         break;
    ```
    
    }
    
    return base_memory;
    }
  
  /**
  - @brief Validate backend configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    // Basic validation
    if (global_max_connections == 0) {
    return false;
    }
    
    // Backend-specific validation
    switch (type) {
    case RuntimeBackend::LIBUV:
    return libuv.thread_pool_size > 0 &&
    libuv.max_connections > 0 &&
    libuv.write_buffer_size > 0 &&
    libuv.read_buffer_size > 0;
    
    ```
     case RuntimeBackend::EPOLL:
         return epoll.max_events > 0 && 
                epoll.initial_fd_capacity > 0;
         
     case RuntimeBackend::IO_URING:
         return io_uring.queue_depth > 0 && 
                io_uring.submit_batch_size > 0 &&
                io_uring.completion_batch_size > 0 &&
                (!io_uring.use_fixed_buffers || 
                 (io_uring.fixed_buffer_count > 0 && io_uring.fixed_buffer_size > 0));
         
     case RuntimeBackend::STELLANE:
         return stellane.io_threads > 0 && 
                stellane.max_connections_per_thread > 0 &&
                stellane.buffer_pool_size > 0 &&
                stellane.buffer_size > 0;
         
     case RuntimeBackend::CUSTOM:
         return static_cast<bool>(custom_factory);
         
     default:
         return false;
    ```
    
    }
    }
  
  /**
  - @brief Auto-configure based on detected hardware
    */
    void auto_configure_for_hardware() {
    // This would typically detect CPU cores, memory, etc.
    // and adjust settings accordingly
    // Implementation would go in the .cpp file
    }
    };

// ============================================================================
// Bitwise operators for EpollFlags
// ============================================================================

constexpr EpollConfig::EpollFlags operator|(EpollConfig::EpollFlags a, EpollConfig::EpollFlags b) {
return static_cast<EpollConfig::EpollFlags>(
static_cast<int>(a) | static_cast<int>(b)
);
}

constexpr EpollConfig::EpollFlags operator&(EpollConfig::EpollFlags a, EpollConfig::EpollFlags b) {
return static_cast<EpollConfig::EpollFlags>(
static_cast<int>(a) & static_cast<int>(b)
);
}

// ============================================================================
// Bitwise operators for IoUringConfig::SetupFlags
// ============================================================================

constexpr IoUringConfig::SetupFlags operator|(IoUringConfig::SetupFlags a, IoUringConfig::SetupFlags b) {
return static_cast<IoUringConfig::SetupFlags>(
static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
);
}

constexpr IoUringConfig::SetupFlags operator&(IoUringConfig::SetupFlags a, IoUringConfig::SetupFlags b) {
return static_cast<IoUringConfig::SetupFlags>(
static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
);
}

} // namespace stellane
