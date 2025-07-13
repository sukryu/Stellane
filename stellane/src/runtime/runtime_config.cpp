#include “stellane/runtime/runtime_config.h”
#include “stellane/utils/logger.h”

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>
#include <regex>
#include <unordered_map>

// External dependencies
#ifdef STELLANE_HAS_TOML11
#include <toml11/toml.hpp>
#endif

#ifdef **linux**
#include <numa.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <sysinfoapi.h>
#elif defined(**APPLE**)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#endif

namespace stellane {

// ============================================================================
// Internal Utilities
// ============================================================================

namespace {

/**

- @brief Trim whitespace from string
  */
  std::string trim(const std::string& str) {
  size_t start = str.find_first_not_of(” \t\n\r”);
  if (start == std::string::npos) return “”;
  
  size_t end = str.find_last_not_of(” \t\n\r”);
  return str.substr(start, end - start + 1);
  }

/**

- @brief Convert string to lowercase
  */
  std::string to_lower(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(),
  [](char c) { return std::tolower(c); });
  return str;
  }

/**

- @brief Parse duration string (e.g., “100ms”, “5s”, “2min”)
  */
  std::optional<std::chrono::milliseconds> parse_duration(const std::string& duration_str) {
  std::regex duration_regex(R”(^(\d+)(ms|s|m|min|sec)$)”);
  std::smatch match;
  
  if (!std::regex_match(duration_str, match, duration_regex)) {
  return std::nullopt;
  }
  
  int value = std::stoi(match[1].str());
  std::string unit = to_lower(match[2].str());
  
  if (unit == “ms”) {
  return std::chrono::milliseconds(value);
  } else if (unit == “s” || unit == “sec”) {
  return std::chrono::milliseconds(value * 1000);
  } else if (unit == “m” || unit == “min”) {
  return std::chrono::milliseconds(value * 60 * 1000);
  }
  
  return std::nullopt;
  }

/**

- @brief Get environment variable safely
  */
  std::optional<std::string> get_env(const std::string& name) {
  const char* value = std::getenv(name.c_str());
  return value ? std::optional<std::string>(value) : std::nullopt;
  }

/**

- @brief Parse boolean from string (true/false, yes/no, 1/0)
  */
  std::optional<bool> parse_bool(const std::string& value) {
  std::string lower_value = to_lower(trim(value));
  
  if (lower_value == “true” || lower_value == “yes” || lower_value == “1” || lower_value == “on”) {
  return true;
  } else if (lower_value == “false” || lower_value == “no” || lower_value == “0” || lower_value == “off”) {
  return false;
  }
  
  return std::nullopt;
  }

/**

- @brief Detect hardware capabilities
  */
  struct HardwareInfo {
  size_t cpu_cores = 0;
  size_t numa_nodes = 0;
  bool has_numa = false;
  bool has_io_uring = false;
  bool has_epoll = false;
  size_t memory_gb = 0;
  };

HardwareInfo detect_hardware() {
HardwareInfo info;

```
// CPU cores detection
info.cpu_cores = std::thread::hardware_concurrency();
if (info.cpu_cores == 0) {
    info.cpu_cores = 1; // Fallback
}
```

#ifdef **linux**
// NUMA detection
if (numa_available() != -1) {
info.has_numa = true;
info.numa_nodes = numa_max_node() + 1;
}

```
// Memory detection
struct sysinfo si;
if (sysinfo(&si) == 0) {
    info.memory_gb = si.totalram * si.mem_unit / (1024 * 1024 * 1024);
}

// io_uring availability (Linux 5.1+)
info.has_io_uring = access("/sys/kernel/btf/io_uring", F_OK) == 0;
info.has_epoll = true; // Always available on Linux
```

#elif defined(_WIN32)
SYSTEM_INFO sys_info;
GetSystemInfo(&sys_info);
info.cpu_cores = sys_info.dwNumberOfProcessors;

```
MEMORYSTATUSEX mem_info;
mem_info.dwLength = sizeof(MEMORYSTATUSEX);
if (GlobalMemoryStatusEx(&mem_info)) {
    info.memory_gb = mem_info.ullTotalPhys / (1024 * 1024 * 1024);
}
```

#elif defined(**APPLE**)
size_t len = sizeof(info.cpu_cores);
sysctlbyname(“hw.ncpu”, &info.cpu_cores, &len, nullptr, 0);

```
int64_t memory_bytes;
len = sizeof(memory_bytes);
sysctlbyname("hw.memsize", &memory_bytes, &len, nullptr, 0);
info.memory_gb = memory_bytes / (1024 * 1024 * 1024);
```

#endif

```
return info;
```

}

} // anonymous namespace

// ============================================================================
// Enum Conversion Functions
// ============================================================================

std::string to_string(RuntimeBackend backend) {
switch (backend) {
case RuntimeBackend::LIBUV: return “libuv”;
case RuntimeBackend::EPOLL: return “epoll”;
case RuntimeBackend::IO_URING: return “io_uring”;
case RuntimeBackend::STELLANE: return “stellane”;
case RuntimeBackend::CUSTOM: return “custom”;
default: return “unknown”;
}
}

std::optional<RuntimeBackend> backend_from_string(const std::string& backend_str) {
std::string lower_str = to_lower(trim(backend_str));

```
if (lower_str == "libuv") return RuntimeBackend::LIBUV;
if (lower_str == "epoll") return RuntimeBackend::EPOLL;
if (lower_str == "io_uring" || lower_str == "iouring") return RuntimeBackend::IO_URING;
if (lower_str == "stellane") return RuntimeBackend::STELLANE;
if (lower_str == "custom") return RuntimeBackend::CUSTOM;

return std::nullopt;
```

}

std::string to_string(TaskSchedulingStrategy strategy) {
switch (strategy) {
case TaskSchedulingStrategy::FIFO: return “fifo”;
case TaskSchedulingStrategy::PRIORITY: return “priority”;
case TaskSchedulingStrategy::WORK_STEALING: return “work_stealing”;
case TaskSchedulingStrategy::AFFINITY: return “affinity”;
case TaskSchedulingStrategy::CUSTOM: return “custom”;
default: return “unknown”;
}
}

std::optional<TaskSchedulingStrategy> strategy_from_string(const std::string& strategy_str) {
std::string lower_str = to_lower(trim(strategy_str));

```
if (lower_str == "fifo") return TaskSchedulingStrategy::FIFO;
if (lower_str == "priority") return TaskSchedulingStrategy::PRIORITY;
if (lower_str == "work_stealing" || lower_str == "work-stealing") return TaskSchedulingStrategy::WORK_STEALING;
if (lower_str == "affinity") return TaskSchedulingStrategy::AFFINITY;
if (lower_str == "custom") return TaskSchedulingStrategy::CUSTOM;

return std::nullopt;
```

}

// ============================================================================
// Platform Detection Functions
// ============================================================================

std::vector<RuntimeBackend> get_available_backends() {
std::vector<RuntimeBackend> backends;

```
// libuv is always available (cross-platform)
backends.push_back(RuntimeBackend::LIBUV);
```

#ifdef **linux**
// epoll is Linux-specific
backends.push_back(RuntimeBackend::EPOLL);

```
// io_uring requires Linux 5.1+
if (access("/sys/kernel/btf/io_uring", F_OK) == 0) {
    backends.push_back(RuntimeBackend::IO_URING);
}
```

#endif

```
// Stellane custom backend (always available)
backends.push_back(RuntimeBackend::STELLANE);
backends.push_back(RuntimeBackend::CUSTOM);

return backends;
```

}

bool is_backend_available(RuntimeBackend backend) {
auto available = get_available_backends();
return std::find(available.begin(), available.end(), backend) != available.end();
}

RuntimeConfig auto_detect_optimal_config() {
auto hw = detect_hardware();
RuntimeConfig config;

```
// Backend selection based on platform and capabilities
if (hw.has_io_uring && hw.cpu_cores >= 4) {
    config.backend = RuntimeBackend::IO_URING;
} else if (hw.has_epoll) {
    config.backend = RuntimeBackend::EPOLL;
} else {
    config.backend = RuntimeBackend::LIBUV;
}

// Worker threads: balance between parallelism and context switching
if (hw.cpu_cores <= 2) {
    config.worker.worker_threads = hw.cpu_cores;
} else if (hw.cpu_cores <= 8) {
    config.worker.worker_threads = hw.cpu_cores - 1; // Leave one for main thread
} else {
    config.worker.worker_threads = std::min(hw.cpu_cores, static_cast<size_t>(16)); // Cap at 16
}

// Scheduling strategy based on CPU count
if (hw.cpu_cores >= 4) {
    config.strategy = TaskSchedulingStrategy::WORK_STEALING;
    config.worker.work_stealing.enabled = true;
} else {
    config.strategy = TaskSchedulingStrategy::FIFO;
}

// NUMA optimization for high-core systems
if (hw.has_numa && hw.numa_nodes > 1 && hw.cpu_cores >= 8) {
    config.worker.affinity.respect_numa_topology = true;
    config.performance.numa_aware = true;
}

// Performance settings based on memory
if (hw.memory_gb >= 8) {
    config.performance.zero_copy_io = true;
    config.performance.io_batch_size = 64;
} else {
    config.performance.io_batch_size = 32;
}

// CPU affinity for high-performance systems
if (hw.cpu_cores >= 8) {
    config.worker.affinity.isolate_main_thread = true;
    config.performance.enable_cpu_affinity = true;
}

return config;
```

}

// ============================================================================
// RuntimeConfig Factory Methods
// ============================================================================

RuntimeConfig RuntimeConfig::production() {
auto config = auto_detect_optimal_config();

```
// Production-specific overrides
config.performance.enable_profiling = false;
config.logging.enable_trace_logging = false;
config.logging.log_task_lifecycle = false;
config.logging.log_worker_stats = true;

// Recovery enabled for production
config.recovery.enabled = true;
config.recovery.backend = "rocksdb";
config.recovery.max_attempts = 3;
config.recovery.timeout = std::chrono::seconds(30);

// Conservative timeouts
config.performance.idle_timeout = std::chrono::milliseconds(100);

return config;
```

}

RuntimeConfig RuntimeConfig::development() {
RuntimeConfig config;

```
// Simple configuration for development
config.backend = RuntimeBackend::LIBUV;
config.strategy = TaskSchedulingStrategy::FIFO;
config.worker.worker_threads = 2;

// Development-friendly settings
config.performance.enable_profiling = true;
config.logging.enable_trace_logging = true;
config.logging.log_task_lifecycle = true;
config.logging.log_worker_stats = true;

// Disable recovery for faster iteration
config.recovery.enabled = false;

// Fast response for development
config.performance.idle_timeout = std::chrono::milliseconds(10);

return config;
```

}

RuntimeConfig RuntimeConfig::high_performance() {
auto config = auto_detect_optimal_config();

```
// High-performance overrides
config.performance.zero_copy_io = true;
config.performance.io_batch_size = 128;
config.performance.enable_cpu_affinity = true;
config.performance.numa_aware = true;

// Aggressive work-stealing
config.worker.work_stealing.enabled = true;
config.worker.work_stealing.steal_threshold = 5;
config.worker.work_stealing.steal_interval = std::chrono::microseconds(50);

// Minimal idle timeout
config.performance.idle_timeout = std::chrono::milliseconds(1);

// Disable logging overhead
config.logging.enable_trace_logging = false;
config.logging.log_task_lifecycle = false;
config.performance.enable_profiling = false;

// More aggressive task processing
config.worker.max_tasks_per_loop = 2000;

return config;
```

}

// ============================================================================
// Environment Variable Support
// ============================================================================

RuntimeConfig RuntimeConfig::from_environment() {
RuntimeConfig config;

```
// Backend selection
if (auto backend_str = get_env("STELLANE_RUNTIME_BACKEND")) {
    if (auto backend = backend_from_string(*backend_str)) {
        config.backend = *backend;
    }
}

// Scheduling strategy
if (auto strategy_str = get_env("STELLANE_RUNTIME_STRATEGY")) {
    if (auto strategy = strategy_from_string(*strategy_str)) {
        config.strategy = *strategy;
    }
}

// Worker threads
if (auto workers_str = get_env("STELLANE_RUNTIME_WORKERS")) {
    try {
        size_t workers = std::stoull(*workers_str);
        if (workers > 0 && workers <= 128) {
            config.worker.worker_threads = workers;
        }
    } catch (...) {
        // Ignore invalid values
    }
}

// CPU affinity
if (auto affinity_str = get_env("STELLANE_RUNTIME_CPU_AFFINITY")) {
    if (auto affinity = parse_bool(*affinity_str)) {
        config.performance.enable_cpu_affinity = *affinity;
    }
}

// NUMA awareness
if (auto numa_str = get_env("STELLANE_RUNTIME_NUMA_AWARE")) {
    if (auto numa = parse_bool(*numa_str)) {
        config.performance.numa_aware = *numa;
    }
}

// Zero-copy I/O
if (auto zero_copy_str = get_env("STELLANE_RUNTIME_ZERO_COPY")) {
    if (auto zero_copy = parse_bool(*zero_copy_str)) {
        config.performance.zero_copy_io = *zero_copy;
    }
}

// I/O batch size
if (auto batch_str = get_env("STELLANE_RUNTIME_IO_BATCH_SIZE")) {
    try {
        int batch_size = std::stoi(*batch_str);
        if (batch_size > 0 && batch_size <= 1024) {
            config.performance.io_batch_size = batch_size;
        }
    } catch (...) {
        // Ignore invalid values
    }
}

// Idle timeout
if (auto timeout_str = get_env("STELLANE_RUNTIME_IDLE_TIMEOUT")) {
    if (auto timeout = parse_duration(*timeout_str)) {
        config.performance.idle_timeout = *timeout;
    }
}

// Recovery settings
if (auto recovery_str = get_env("STELLANE_RUNTIME_RECOVERY_ENABLED")) {
    if (auto recovery = parse_bool(*recovery_str)) {
        config.recovery.enabled = *recovery;
    }
}

if (auto recovery_backend_str = get_env("STELLANE_RUNTIME_RECOVERY_BACKEND")) {
    config.recovery.backend = *recovery_backend_str;
}

if (auto recovery_path_str = get_env("STELLANE_RUNTIME_RECOVERY_PATH")) {
    config.recovery.path = *recovery_path_str;
}

// Profiling
if (auto profiling_str = get_env("STELLANE_RUNTIME_PROFILING")) {
    if (auto profiling = parse_bool(*profiling_str)) {
        config.performance.enable_profiling = *profiling;
    }
}

return config;
```

}

RuntimeConfig RuntimeConfig::with_environment_overrides() const {
RuntimeConfig result = *this;
RuntimeConfig env_config = from_environment();

```
return result.merge(env_config);
```

}

// ============================================================================
// TOML Configuration Support
// ============================================================================

#ifdef STELLANE_HAS_TOML11

std::optional<RuntimeConfig> RuntimeConfig::from_toml_file(const std::filesystem::path& config_path) {
try {
if (!std::filesystem::exists(config_path)) {
return std::nullopt;
}

```
    auto toml_data = toml::parse(config_path);
    return from_toml_data(toml_data);
} catch (const std::exception& e) {
    // Log error but don't throw
    return std::nullopt;
}
```

}

std::optional<RuntimeConfig> RuntimeConfig::from_toml_string(const std::string& toml_content) {
try {
auto toml_data = toml::parse(toml_content);
return from_toml_data(toml_data);
} catch (const std::exception& e) {
return std::nullopt;
}
}

std::optional<RuntimeConfig> RuntimeConfig::from_toml_data(const toml::value& toml_data) {
RuntimeConfig config;

```
if (toml_data.contains("runtime")) {
    const auto& runtime_section = toml::find(toml_data, "runtime");
    
    // Backend
    if (runtime_section.contains("backend")) {
        std::string backend_str = toml::find<std::string>(runtime_section, "backend");
        if (auto backend = backend_from_string(backend_str)) {
            config.backend = *backend;
        }
    }
    
    // Scheduling strategy
    if (runtime_section.contains("strategy")) {
        std::string strategy_str = toml::find<std::string>(runtime_section, "strategy");
        if (auto strategy = strategy_from_string(strategy_str)) {
            config.strategy = *strategy;
        }
    }
    
    // Worker threads
    if (runtime_section.contains("worker_threads")) {
        config.worker.worker_threads = toml::find<size_t>(runtime_section, "worker_threads");
    }
    
    // Max tasks per loop
    if (runtime_section.contains("max_tasks_per_loop")) {
        config.worker.max_tasks_per_loop = toml::find<size_t>(runtime_section, "max_tasks_per_loop");
    }
    
    // CPU affinity
    if (runtime_section.contains("enable_cpu_affinity")) {
        config.performance.enable_cpu_affinity = toml::find<bool>(runtime_section, "enable_cpu_affinity");
    }
    
    // NUMA awareness
    if (runtime_section.contains("numa_aware")) {
        config.performance.numa_aware = toml::find<bool>(runtime_section, "numa_aware");
    }
    
    // Work-stealing subsection
    if (runtime_section.contains("work_stealing")) {
        const auto& ws_section = toml::find(runtime_section, "work_stealing");
        
        if (ws_section.contains("enabled")) {
            config.worker.work_stealing.enabled = toml::find<bool>(ws_section, "enabled");
        }
        
        if (ws_section.contains("steal_threshold")) {
            config.worker.work_stealing.steal_threshold = toml::find<size_t>(ws_section, "steal_threshold");
        }
        
        if (ws_section.contains("steal_interval")) {
            std::string interval_str = toml::find<std::string>(ws_section, "steal_interval");
            if (auto interval = parse_duration(interval_str)) {
                config.worker.work_stealing.steal_interval = 
                    std::chrono::duration_cast<std::chrono::microseconds>(*interval);
            }
        }
    }
}

// Performance section
if (toml_data.contains("performance")) {
    const auto& perf_section = toml::find(toml_data, "performance");
    
    if (perf_section.contains("zero_copy_io")) {
        config.performance.zero_copy_io = toml::find<bool>(perf_section, "zero_copy_io");
    }
    
    if (perf_section.contains("io_batch_size")) {
        config.performance.io_batch_size = toml::find<int>(perf_section, "io_batch_size");
    }
    
    if (perf_section.contains("idle_timeout")) {
        std::string timeout_str = toml::find<std::string>(perf_section, "idle_timeout");
        if (auto timeout = parse_duration(timeout_str)) {
            config.performance.idle_timeout = *timeout;
        }
    }
    
    if (perf_section.contains("enable_profiling")) {
        config.performance.enable_profiling = toml::find<bool>(perf_section, "enable_profiling");
    }
}

// Recovery section
if (toml_data.contains("recovery")) {
    const auto& recovery_section = toml::find(toml_data, "recovery");
    
    if (recovery_section.contains("enabled")) {
        config.recovery.enabled = toml::find<bool>(recovery_section, "enabled");
    }
    
    if (recovery_section.contains("backend")) {
        config.recovery.backend = toml::find<std::string>(recovery_section, "backend");
    }
    
    if (recovery_section.contains("path")) {
        config.recovery.path = toml::find<std::string>(recovery_section, "path");
    }
    
    if (recovery_section.contains("max_attempts")) {
        config.recovery.max_attempts = toml::find<int>(recovery_section, "max_attempts");
    }
    
    if (recovery_section.contains("timeout")) {
        std::string timeout_str = toml::find<std::string>(recovery_section, "timeout");
        if (auto timeout = parse_duration(timeout_str)) {
            config.recovery.timeout = *timeout;
        }
    }
}

return config;
```

}

#else // !STELLANE_HAS_TOML11

std::optional<RuntimeConfig> RuntimeConfig::from_toml_file(const std::filesystem::path& config_path) {
// TOML support not compiled in
return std::nullopt;
}

std::optional<RuntimeConfig> RuntimeConfig::from_toml_string(const std::string& toml_content) {
// TOML support not compiled in
return std::nullopt;
}

#endif // STELLANE_HAS_TOML11

// ============================================================================
// Configuration Validation
// ============================================================================

ValidationResult RuntimeConfig::validate() const {
ValidationResult result;
result.is_valid = true;

```
// Backend availability check
if (!is_backend_available(backend)) {
    result.is_valid = false;
    result.errors.push_back("Backend '" + to_string(backend) + "' is not available on this platform");
}

// Worker thread validation
if (worker.worker_threads == 0) {
    result.is_valid = false;
    result.errors.push_back("Worker thread count must be greater than 0");
}

if (worker.worker_threads > 128) {
    result.warnings.push_back("Worker thread count (" + std::to_string(worker.worker_threads) + 
                              ") is very high and may cause performance issues");
}

// Max tasks per loop validation
if (worker.max_tasks_per_loop == 0) {
    result.is_valid = false;
    result.errors.push_back("Max tasks per loop must be greater than 0");
}

if (worker.max_tasks_per_loop > 10000) {
    result.warnings.push_back("Max tasks per loop is very high and may cause latency spikes");
}

// I/O batch size validation
if (performance.io_batch_size <= 0) {
    result.is_valid = false;
    result.errors.push_back("I/O batch size must be positive");
}

if (performance.io_batch_size > 1024) {
    result.warnings.push_back("I/O batch size is very high and may increase memory usage");
}

// Recovery path validation
if (recovery.enabled) {
    if (recovery.path.empty()) {
        result.is_valid = false;
        result.errors.push_back("Recovery path must be specified when recovery is enabled");
    } else {
        std::filesystem::path recovery_dir = std::filesystem::path(recovery.path).parent_path();
        if (!std::filesystem::exists(recovery_dir)) {
            result.warnings.push_back("Recovery directory does not exist: " + recovery_dir.string());
        }
    }
    
    if (recovery.max_attempts <= 0) {
        result.is_valid = false;
        result.errors.push_back("Recovery max attempts must be positive");
    }
}

// Work-stealing validation
if (worker.work_stealing.enabled && strategy != TaskSchedulingStrategy::WORK_STEALING) {
    result.warnings.push_back("Work-stealing is enabled but scheduling strategy is not WORK_STEALING");
}

return result;
```

}

// ============================================================================
// Configuration Serialization
// ============================================================================

std::string RuntimeConfig::to_toml(bool pretty_print) const {
std::ostringstream oss;

```
if (pretty_print) {
    oss << "# Stellane Runtime Configuration\n";
    oss << "# Generated automatically\n\n";
}

// Runtime section
oss << "[runtime]\n";
oss << "backend = \"" << to_string(backend) << "\"\n";
oss << "strategy = \"" << to_string(strategy) << "\"\n";
oss << "worker_threads = " << worker.worker_threads << "\n";
oss << "max_tasks_per_loop = " << worker.max_tasks_per_loop << "\n";

if (pretty_print) oss << "\n";

// Work-stealing subsection
oss << "[runtime.work_stealing]\n";
oss << "enabled = " << (worker.work_stealing.enabled ? "true" : "false") << "\n";
oss << "steal_threshold = " << worker.work_stealing.steal_threshold << "\n";
oss << "steal_interval = \"" << worker.work_stealing.steal_interval.count() << "us\"\n";

if (pretty_print) oss << "\n";

// Performance section
oss << "[performance]\n";
oss << "zero_copy_io = " << (performance.zero_copy_io ? "true" : "false") << "\n";
oss << "io_batch_size = " << performance.io_batch_size << "\n";
oss << "idle_timeout = \"" << performance.idle_timeout.count() << "ms\"\n";
oss << "enable_profiling = " << (performance.enable_profiling ? "true" : "false") << "\n";
oss << "enable_cpu_affinity = " << (performance.enable_cpu_affinity ? "true" : "false") << "\n";
oss << "numa_aware = " << (performance.numa_aware ? "true" : "false") << "\n";

if (pretty_print) oss << "\n";

// Recovery section
oss << "[recovery]\n";
oss << "enabled = " << (recovery.enabled ? "true" : "false") << "\n";
oss << "backend = \"" << recovery.backend << "\"\n";
oss << "path = \"" << recovery.path << "\"\n";
oss << "max_attempts = " << recovery.max_attempts << "\n";
oss << "timeout = \"" << recovery.timeout.count() << "ms\"\n";

return oss.str();
```

}

RuntimeConfig RuntimeConfig::merge(const RuntimeConfig& other) const {
RuntimeConfig result = *this;

```
// Simple field merging - other takes precedence
result.backend = other.backend;
result.strategy = other.strategy;

// Worker configuration
result.worker.worker_threads = other.worker.worker_threads;
result.worker.max_tasks_per_loop = other.worker.max_tasks_per_loop;
result.worker.affinity = other.worker.affinity;
result.worker.work_stealing = other.worker.work_stealing;

// Performance configuration
result.performance = other.performance;

// Recovery configuration  
result.recovery = other.recovery;

// Logging configuration
result.logging = other.logging;

// Experimental features
result.experimental = other.experimental;

return result;
```

}

// ============================================================================
// Utility Methods (continued from runtime_config.cpp)
// ============================================================================

size_t RuntimeConfig::estimated_memory_usage() const {
size_t base_memory = 0;

```
// Base runtime overhead (approximate)
base_memory += 10 * 1024 * 1024; // 10MB base

// Worker thread overhead
size_t worker_stack_size = 2 * 1024 * 1024; // 2MB per worker stack
base_memory += worker.worker_threads * worker_stack_size;

// Task queue memory (estimated)
size_t task_queue_memory = worker.max_tasks_per_loop * sizeof(void*) * worker.worker_threads;
base_memory += task_queue_memory;

// I/O buffer memory
if (performance.zero_copy_io) {
    base_memory += performance.io_batch_size * 64 * 1024; // 64KB per batch slot
} else {
    base_memory += performance.io_batch_size * 4 * 1024; // 4KB per batch slot
}

// Backend-specific memory
switch (backend) {
    case RuntimeBackend::IO_URING:
        // io_uring submission/completion queues
        base_memory += 1024 * 1024; // 1MB for rings
        break;
    case RuntimeBackend::EPOLL:
        // epoll event buffer
        base_memory += 64 * 1024; // 64KB for events
        break;
    case RuntimeBackend::LIBUV:
        // libuv internal structures
        base_memory += 512 * 1024; // 512KB overhead
        break;
    case RuntimeBackend::STELLANE:
        // Custom backend overhead
        base_memory += 2 * 1024 * 1024; // 2MB
        break;
    default:
        base_memory += 1024 * 1024; // 1MB default
        break;
}

// Recovery system memory
if (recovery.enabled) {
    base_memory += 5 * 1024 * 1024; // 5MB for recovery journal
}

// Profiling overhead
if (performance.enable_profiling) {
    base_memory += 10 * 1024 * 1024; // 10MB for profiling data
}

return base_memory;
```

}

PlatformCompatibility RuntimeConfig::check_platform_compatibility() const {
PlatformCompatibility compat;
compat.is_compatible = true;

```
// Check backend compatibility
if (!is_backend_available(backend)) {
    compat.is_compatible = false;
    compat.issues.push_back("Backend '" + to_string(backend) + "' is not available on this platform");
    
    // Suggest alternatives
    auto available = get_available_backends();
    if (!available.empty()) {
        std::string suggestion = "Available backends: ";
        for (size_t i = 0; i < available.size(); ++i) {
            if (i > 0) suggestion += ", ";
            suggestion += to_string(available[i]);
        }
        compat.suggestions.push_back(suggestion);
    }
}

// Check NUMA support
if (performance.numa_aware || worker.affinity.respect_numa_topology) {
```

#ifdef **linux**
if (numa_available() == -1) {
compat.warnings.push_back(“NUMA awareness requested but NUMA is not available”);
compat.suggestions.push_back(“Disable NUMA settings or install NUMA libraries”);
}
#else
compat.warnings.push_back(“NUMA awareness requested but not supported on this platform”);
compat.suggestions.push_back(“NUMA features are only available on Linux”);
#endif
}

```
// Check worker thread count vs CPU cores
auto hw = detect_hardware();
if (worker.worker_threads > hw.cpu_cores * 2) {
    compat.warnings.push_back("Worker thread count (" + std::to_string(worker.worker_threads) + 
                              ") exceeds 2x CPU cores (" + std::to_string(hw.cpu_cores) + ")");
    compat.suggestions.push_back("Consider reducing worker_threads to " + 
                                 std::to_string(hw.cpu_cores) + " or less");
}

// Check memory requirements
size_t estimated_mem = estimated_memory_usage();
if (estimated_mem > hw.memory_gb * 1024 * 1024 * 1024 / 4) { // > 25% of system memory
    compat.warnings.push_back("Estimated memory usage (" + 
                              std::to_string(estimated_mem / (1024 * 1024)) + 
                              "MB) is high relative to system memory (" + 
                              std::to_string(hw.memory_gb) + "GB)");
    compat.suggestions.push_back("Consider reducing worker_threads or disabling profiling");
}

// Check io_uring specific requirements
if (backend == RuntimeBackend::IO_URING) {
```

#ifdef **linux**
// Check kernel version (simplified check)
std::ifstream version_file(”/proc/version”);
if (version_file.is_open()) {
std::string version_line;
std::getline(version_file, version_line);

```
        // Very basic kernel version check
        if (version_line.find("5.") == std::string::npos && 
            version_line.find("6.") == std::string::npos) {
            compat.warnings.push_back("io_uring requires Linux kernel 5.1 or later");
            compat.suggestions.push_back("Upgrade kernel or use epoll backend");
        }
    }
```

#else
compat.is_compatible = false;
compat.issues.push_back(“io_uring backend is only available on Linux”);
compat.suggestions.push_back(“Use libuv backend for cross-platform compatibility”);
#endif
}

```
// Check recovery backend requirements
if (recovery.enabled) {
    if (recovery.backend == "rocksdb") {
        // Check if RocksDB is available (would need compile-time detection)
        compat.warnings.push_back("RocksDB recovery backend requires RocksDB library");
        compat.suggestions.push_back("Ensure RocksDB is installed or use 'mmap' backend");
    }
    
    // Check recovery path writability
    std::filesystem::path recovery_dir = std::filesystem::path(recovery.path).parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(recovery_dir, ec)) {
        compat.warnings.push_back("Recovery directory does not exist: " + recovery_dir.string());
        compat.suggestions.push_back("Create recovery directory or choose existing path");
    }
}

return compat;
```

}

std::string RuntimeConfig::summary() const {
std::ostringstream oss;

```
oss << "Stellane Runtime Configuration Summary:\n";
oss << "======================================\n\n";

// Core settings
oss << "Core Settings:\n";
oss << "  Backend: " << to_string(backend) << "\n";
oss << "  Scheduling: " << to_string(strategy) << "\n";
oss << "  Worker Threads: " << worker.worker_threads << "\n";
oss << "  Max Tasks/Loop: " << worker.max_tasks_per_loop << "\n\n";

// Performance settings
oss << "Performance:\n";
oss << "  Zero-Copy I/O: " << (performance.zero_copy_io ? "Enabled" : "Disabled") << "\n";
oss << "  I/O Batch Size: " << performance.io_batch_size << "\n";
oss << "  CPU Affinity: " << (performance.enable_cpu_affinity ? "Enabled" : "Disabled") << "\n";
oss << "  NUMA Aware: " << (performance.numa_aware ? "Enabled" : "Disabled") << "\n";
oss << "  Profiling: " << (performance.enable_profiling ? "Enabled" : "Disabled") << "\n";
oss << "  Idle Timeout: " << performance.idle_timeout.count() << "ms\n\n";

// Work-stealing (if applicable)
if (strategy == TaskSchedulingStrategy::WORK_STEALING || worker.work_stealing.enabled) {
    oss << "Work-Stealing:\n";
    oss << "  Enabled: " << (worker.work_stealing.enabled ? "Yes" : "No") << "\n";
    oss << "  Steal Threshold: " << worker.work_stealing.steal_threshold << "\n";
    oss << "  Steal Interval: " << worker.work_stealing.steal_interval.count() << "μs\n\n";
}

// Recovery settings
oss << "Recovery:\n";
oss << "  Enabled: " << (recovery.enabled ? "Yes" : "No") << "\n";
if (recovery.enabled) {
    oss << "  Backend: " << recovery.backend << "\n";
    oss << "  Path: " << recovery.path << "\n";
    oss << "  Max Attempts: " << recovery.max_attempts << "\n";
    oss << "  Timeout: " << recovery.timeout.count() << "ms\n";
}
oss << "\n";

// Resource estimates
oss << "Resource Estimates:\n";
oss << "  Memory Usage: ~" << (estimated_memory_usage() / (1024 * 1024)) << "MB\n";

// Platform compatibility
auto compat = check_platform_compatibility();
oss << "  Platform Compatible: " << (compat.is_compatible ? "Yes" : "No") << "\n";

if (!compat.warnings.empty()) {
    oss << "\nWarnings:\n";
    for (const auto& warning : compat.warnings) {
        oss << "  - " << warning << "\n";
    }
}

if (!compat.suggestions.empty()) {
    oss << "\nSuggestions:\n";
    for (const auto& suggestion : compat.suggestions) {
        oss << "  - " << suggestion << "\n";
    }
}

return oss.str();
```

}

} // namespace stellane

// ============================================================================
// Additional Helper Functions (Non-member)
// ============================================================================

namespace stellane {

/**

- @brief Create a RuntimeConfig with sane defaults for common use cases
  */
  RuntimeConfig create_config_for_use_case(const std::string& use_case) {
  std::string lower_case = to_lower(trim(use_case));
  
  if (lower_case == “chat” || lower_case == “realtime” || lower_case == “gaming”) {
  // Real-time applications need low latency
  auto config = RuntimeConfig::high_performance();
  
  ```
   // Ultra-low latency settings
   config.performance.idle_timeout = std::chrono::microseconds(500);
   config.worker.work_stealing.steal_interval = std::chrono::microseconds(25);
   config.worker.max_tasks_per_loop = 500; // Smaller batches for lower latency
   
   // Disable recovery for minimum latency (can be re-enabled if needed)
   config.recovery.enabled = false;
   
   return config;
  ```
  
  } else if (lower_case == “web” || lower_case == “api” || lower_case == “http”) {
  // Web/API servers need balanced performance
  auto config = RuntimeConfig::production();
  
  ```
   // Moderate settings for web workloads
   config.performance.idle_timeout = std::chrono::milliseconds(50);
   config.worker.max_tasks_per_loop = 1000;
   
   return config;
  ```
  
  } else if (lower_case == “batch” || lower_case == “background” || lower_case == “etl”) {
  // Batch processing prioritizes throughput over latency
  auto config = RuntimeConfig::production();
  
  ```
   // High throughput settings
   config.performance.idle_timeout = std::chrono::milliseconds(200);
   config.worker.max_tasks_per_loop = 5000; // Large batches
   config.performance.io_batch_size = 256;
   
   // Work-stealing for better load distribution
   config.strategy = TaskSchedulingStrategy::WORK_STEALING;
   config.worker.work_stealing.enabled = true;
   config.worker.work_stealing.steal_threshold = 50;
   
   return config;
  ```
  
  } else if (lower_case == “microservice” || lower_case == “cloud”) {
  // Microservices need balanced resource usage
  auto config = RuntimeConfig::production();
  
  ```
   // Conservative resource usage
   auto hw = detect_hardware();
   config.worker.worker_threads = std::min(hw.cpu_cores / 2, static_cast<size_t>(4));
   
   // Enable recovery for resilience
   config.recovery.enabled = true;
   config.recovery.max_attempts = 5;
   
   return config;
  ```
  
  } else {
  // Default to production config
  return RuntimeConfig::production();
  }
  }

/**

- @brief Validate and auto-correct configuration issues
  */
  RuntimeConfig auto_correct_config(const RuntimeConfig& input_config) {
  RuntimeConfig config = input_config;
  auto hw = detect_hardware();
  
  // Auto-correct backend if not available
  if (!is_backend_available(config.backend)) {
  auto available = get_available_backends();
  if (!available.empty()) {
  // Prefer io_uring > epoll > libuv > stellane > custom
  if (std::find(available.begin(), available.end(), RuntimeBackend::IO_URING) != available.end()) {
  config.backend = RuntimeBackend::IO_URING;
  } else if (std::find(available.begin(), available.end(), RuntimeBackend::EPOLL) != available.end()) {
  config.backend = RuntimeBackend::EPOLL;
  } else if (std::find(available.begin(), available.end(), RuntimeBackend::LIBUV) != available.end()) {
  config.backend = RuntimeBackend::LIBUV;
  } else {
  config.backend = available[0];
  }
  }
  }
  
  // Auto-correct worker thread count
  if (config.worker.worker_threads == 0) {
  config.worker.worker_threads = 1;
  } else if (config.worker.worker_threads > hw.cpu_cores * 4) {
  // Cap at 4x CPU cores to prevent excessive context switching
  config.worker.worker_threads = hw.cpu_cores * 2;
  }
  
  // Auto-correct max tasks per loop
  if (config.worker.max_tasks_per_loop == 0) {
  config.worker.max_tasks_per_loop = 1000;
  } else if (config.worker.max_tasks_per_loop > 50000) {
  config.worker.max_tasks_per_loop = 10000; // Prevent excessive latency
  }
  
  // Auto-correct I/O batch size
  if (config.performance.io_batch_size <= 0) {
  config.performance.io_batch_size = 32;
  } else if (config.performance.io_batch_size > 1024) {
  config.performance.io_batch_size = 256;
  }
  
  // Auto-correct recovery settings
  if (config.recovery.enabled) {
  if (config.recovery.path.empty()) {
  config.recovery.path = “/tmp/stellane_recovery”;
  }
  
  ```
   if (config.recovery.max_attempts <= 0) {
       config.recovery.max_attempts = 3;
   } else if (config.recovery.max_attempts > 20) {
       config.recovery.max_attempts = 10; // Prevent infinite retry loops
   }
  ```
  
  }
  
  // Disable NUMA features if not available
  #ifndef **linux**
  config.performance.numa_aware = false;
  config.worker.affinity.respect_numa_topology = false;
  #else
  if (numa_available() == -1) {
  config.performance.numa_aware = false;
  config.worker.affinity.respect_numa_topology = false;
  }
  #endif
  
  // Disable work-stealing for single-core systems
  if (hw.cpu_cores == 1) {
  config.strategy = TaskSchedulingStrategy::FIFO;
  config.worker.work_stealing.enabled = false;
  }
  
  return config;
  }

/**

- @brief Load configuration with comprehensive fallback chain
  */
  RuntimeConfig load_config_with_fallbacks(const std::vector<std::string>& config_paths) {
  RuntimeConfig config;
  bool loaded = false;
  
  // Try loading from each path in order
  for (const auto& path : config_paths) {
  auto maybe_config = RuntimeConfig::from_toml_file(path);
  if (maybe_config.has_value()) {
  config = *maybe_config;
  loaded = true;
  break;
  }
  }
  
  // If no file loaded, start with production defaults
  if (!loaded) {
  config = RuntimeConfig::production();
  }
  
  // Apply environment variable overrides
  config = config.with_environment_overrides();
  
  // Auto-correct any issues
  config = auto_correct_config(config);
  
  return config;
  }

/**

- @brief Generate configuration template file
  */
  std::string generate_config_template(const std::string& use_case) {
  auto config = create_config_for_use_case(use_case);
  
  std::ostringstream oss;
  oss << “# Stellane Runtime Configuration Template\n”;
  oss << “# Generated for use case: “ << use_case << “\n”;
  oss << “# Edit this file and save as stellane.config.toml\n\n”;
  
  oss << config.to_toml(true);
  
  oss << “\n# Environment variable overrides:\n”;
  oss << “# STELLANE_RUNTIME_BACKEND - Override backend selection\n”;
  oss << “# STELLANE_RUNTIME_WORKERS - Override worker thread count\n”;
  oss << “# STELLANE_RUNTIME_STRATEGY - Override scheduling strategy\n”;
  oss << “# STELLANE_RUNTIME_CPU_AFFINITY - Enable/disable CPU affinity\n”;
  oss << “# STELLANE_RUNTIME_PROFILING - Enable/disable profiling\n”;
  
  return oss.str();
  }

} // namespace stellane
