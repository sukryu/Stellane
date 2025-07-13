#pragma once

#include <cstdint>

namespace stellane {

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
- - CUSTOM: User-defined custom backend
    */
    enum class RuntimeBackend : int {
    LIBUV = 0,        ///< Cross-platform libuv backend
    EPOLL = 1,        ///< Linux epoll single-threaded backend  
    IO_URING = 2,     ///< Linux io_uring ultra-high performance
    STELLANE = 3,     ///< Custom Stellane multi-threaded backend
    CUSTOM = 999      ///< User-defined custom backend
    };

// ============================================================================
// Task Scheduling Strategies
// ============================================================================

/**

- @brief Task scheduling strategies for different workload patterns
- 
- Each strategy optimizes for different use cases:
- - FIFO: Simple first-in-first-out, predictable latency
- - PRIORITY: Priority-based scheduling for real-time tasks
- - WORK_STEALING: Multi-core optimization with load balancing
- - AFFINITY: CPU/NUMA affinity-based placement
- - ROUND_ROBIN: Even distribution across workers
- - CUSTOM: User-defined scheduling logic
    */
    enum class TaskSchedulingStrategy : int {
    FIFO = 0,           ///< First-in, first-out (simple, predictable)
    PRIORITY = 1,       ///< Priority-based scheduling
    WORK_STEALING = 2,  ///< Work-stealing for multi-core optimization
    AFFINITY = 3,       ///< CPU/worker affinity-based
    ROUND_ROBIN = 4,    ///< Round-robin distribution
    CUSTOM = 999        ///< User-defined scheduling strategy
    };

// ============================================================================
// Task Priority Levels
// ============================================================================

/**

- @brief Task priority levels for priority-based scheduling
- 
- Higher numerical values indicate higher priority.
- Used by PRIORITY scheduling strategy and priority-aware schedulers.
  */
  enum class TaskPriority : int {
  LOWEST = 0,       ///< Background tasks, cleanup operations
  LOW = 25,         ///< Non-critical periodic tasks
  NORMAL = 50,      ///< Default priority for regular tasks
  HIGH = 75,        ///< Important user-facing operations
  HIGHEST = 100,    ///< Critical real-time tasks
  SYSTEM = 255      ///< System-level tasks (internal use)
  };

// ============================================================================
// Recovery System Types
// ============================================================================

/**

- @brief Recovery journal backend implementations
- 
- Different backends offer various trade-offs between performance,
- durability, and complexity.
  */
  enum class RecoveryBackend : int {
  DISABLED = 0,     ///< No recovery journaling
  MMAP = 1,         ///< Memory-mapped file journal
  ROCKSDB = 2,      ///< RocksDB embedded database
  SQLITE = 3,       ///< SQLite database journal
  CUSTOM = 999      ///< User-defined recovery backend
  };

// ============================================================================
// Logging Configuration Types
// ============================================================================

/**

- @brief Logging levels for runtime components
- 
- Controls verbosity of runtime logging output.
  */
  enum class LogLevel : int {
  OFF = 0,          ///< No logging
  ERROR = 1,        ///< Error messages only
  WARN = 2,         ///< Warnings and errors
  INFO = 3,         ///< Informational messages
  DEBUG = 4,        ///< Debug information
  TRACE = 5         ///< Detailed trace information
  };

/**

- @brief Logging output destinations
  */
  enum class LogOutput : int {
  CONSOLE = 0,      ///< Standard output/error
  FILE = 1,         ///< Log file
  SYSLOG = 2,       ///< System log (Unix)
  EVENTLOG = 3,     ///< Windows Event Log
  CUSTOM = 999      ///< User-defined output
  };

// ============================================================================
// Performance Tuning Types
// ============================================================================

/**

- @brief CPU affinity strategies
  */
  enum class AffinityStrategy : int {
  NONE = 0,         ///< No CPU affinity
  ROUND_ROBIN = 1,  ///< Round-robin CPU assignment
  NUMA_AWARE = 2,   ///< NUMA topology-aware assignment
  CUSTOM = 999      ///< User-defined affinity mapping
  };

/**

- @brief Work-stealing policies
  */
  enum class WorkStealingPolicy : int {
  DISABLED = 0,     ///< No work stealing
  AGGRESSIVE = 1,   ///< Steal frequently for maximum load balance
  CONSERVATIVE = 2, ///< Steal only when significantly idle
  ADAPTIVE = 3,     ///< Adjust stealing based on system load
  CUSTOM = 999      ///< User-defined stealing policy
  };

// ============================================================================
// Hardware Detection Types
// ============================================================================

/**

- @brief CPU architecture types
  */
  enum class CpuArchitecture : int {
  UNKNOWN = 0,      ///< Unknown or unsupported architecture
  X86_64 = 1,       ///< Intel/AMD x86-64
  ARM64 = 2,        ///< ARM 64-bit
  ARM32 = 3,        ///< ARM 32-bit
  RISCV = 4,        ///< RISC-V
  OTHER = 999       ///< Other architecture
  };

/**

- @brief Operating system types
  */
  enum class OperatingSystem : int {
  UNKNOWN = 0,      ///< Unknown OS
  LINUX = 1,        ///< Linux
  WINDOWS = 2,      ///< Windows
  MACOS = 3,        ///< macOS
  FREEBSD = 4,      ///< FreeBSD
  OTHER = 999       ///< Other Unix-like OS
  };

// ============================================================================
// Configuration Validation Types
// ============================================================================

/**

- @brief Configuration validation severity levels
  */
  enum class ValidationSeverity : int {
  INFO = 0,         ///< Informational message
  WARNING = 1,      ///< Warning (configuration will work but may be suboptimal)
  ERROR = 2,        ///< Error (configuration is invalid)
  CRITICAL = 3      ///< Critical error (will cause runtime failure)
  };

// ============================================================================
// Experimental Features
// ============================================================================

/**

- @brief Experimental feature flags
- 
- These features are in development and may not be stable.
  */
  enum class ExperimentalFeature : uint32_t {
  NONE = 0,                    ///< No experimental features
  TASK_PRIORITIES = 1 << 0,    ///< Task priority system
  ADAPTIVE_SCALING = 1 << 1,   ///< Adaptive worker scaling
  JIT_COMPILATION = 1 << 2,    ///< JIT compilation for hot paths
  COROUTINE_POOLING = 1 << 3,  ///< Coroutine object pooling
  NUMA_OPTIMIZATION = 1 << 4,  ///< Advanced NUMA optimizations
  PROFILE_GUIDED = 1 << 5,     ///< Profile-guided optimizations
  ALL = 0xFFFFFFFF             ///< All experimental features
  };

// Bitwise operators for ExperimentalFeature
constexpr ExperimentalFeature operator|(ExperimentalFeature a, ExperimentalFeature b) {
return static_cast<ExperimentalFeature>(
static_cast<uint32_t>(a) | static_cast<uint32_t>(b)
);
}

constexpr ExperimentalFeature operator&(ExperimentalFeature a, ExperimentalFeature b) {
return static_cast<ExperimentalFeature>(
static_cast<uint32_t>(a) & static_cast<uint32_t>(b)
);
}

constexpr ExperimentalFeature operator~(ExperimentalFeature a) {
return static_cast<ExperimentalFeature>(~static_cast<uint32_t>(a));
}

constexpr ExperimentalFeature& operator|=(ExperimentalFeature& a, ExperimentalFeature b) {
return a = a | b;
}

constexpr ExperimentalFeature& operator&=(ExperimentalFeature& a, ExperimentalFeature b) {
return a = a & b;
}

/**

- @brief Check if experimental feature is enabled
  */
  constexpr bool has_feature(ExperimentalFeature features, ExperimentalFeature feature) {
  return (features & feature) != ExperimentalFeature::NONE;
  }

} // namespace stellane
