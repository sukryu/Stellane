// include/stellane/runtime/event_loop.h
#pragma once

#include <memory>
#include <functional>
#include <chrono>
#include <string>
#include <atomic>
#include <future>

namespace stellane {

// Forward declarations
template<typename T = void>
class Task;

// ============================================================================
// Core Event Loop Backend Interface
// ============================================================================

/**

- @brief Abstract interface for event loop backends
- 
- This interface provides a unified API for different I/O event loop
- implementations (libuv, epoll, io_uring, etc.)
  */
  class IEventLoopBackend {
  public:
  virtual ~IEventLoopBackend() = default;
  
  /**
  - @brief Start the event loop (blocking call)
  - @return 0 on success, error code on failure
    */
    virtual int run() = 0;
  
  /**
  - @brief Stop the event loop gracefully
  - @param timeout Maximum time to wait for pending operations
    */
    virtual void stop(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) = 0;
  
  /**
  - @brief Schedule a task for execution
  - @param task Task to execute
    */
    virtual void schedule(Task<> task) = 0;
  
  /**
  - @brief Check if the event loop is currently running
  - @return true if running, false otherwise
    */
    virtual bool is_running() const = 0;
  
  /**
  - @brief Get number of pending tasks
  - @return Number of tasks in queue
    */
    virtual size_t pending_tasks() const = 0;
  
  /**
  - @brief Get backend implementation name
  - @return Name of the backend (e.g., “libuv”, “epoll”)
    */
    virtual std::string name() const = 0;
    };

// ============================================================================
// Runtime Configuration
// ============================================================================

/**

- @brief Configuration for the runtime system
  */
  struct RuntimeConfig {
  std::string backend_type = “libuv”;              ///< Backend type
  int worker_threads = 1;                          ///< Number of worker threads (single-loop for now)
  int max_tasks_per_loop = 1000;                   ///< Task queue size per loop
  bool enable_cpu_affinity = false;                ///< Pin workers to CPU cores
  
  // Recovery settings (future)
  bool enable_request_recovery = false;
  std::string recovery_backend = “mmap”;
  std::string recovery_path = “/tmp/stellane_recovery”;
  int max_recovery_attempts = 3;
  
  // Performance tuning
  bool zero_copy_io = true;
  int io_batch_size = 32;
  std::chrono::milliseconds idle_timeout{100};
  
  // Validation
  bool validate() const {
  return worker_threads > 0 &&
  max_tasks_per_loop > 0 &&
  !backend_type.empty();
  }
  };

// ============================================================================
// Runtime Factory and Management
// ============================================================================

/**

- @brief Main runtime factory and management class
  */
  class Runtime {
  public:
  // Static factory methods
  static void initialize(const RuntimeConfig& config = RuntimeConfig{});
  static void initialize_from_file(const std::string& config_path);
  static void shutdown();
  
  // Runtime control
  static int start();  // Blocking call, returns exit code
  static void stop();
  static bool is_running();
  
  // Task scheduling interface
  static void schedule(Task<> task);
  static void schedule_delayed(Task<> task, std::chrono::milliseconds delay);
  
  // Monitoring and stats
  struct RuntimeStats {
  std::chrono::steady_clock::time_point start_time;
  std::chrono::milliseconds uptime;
  size_t total_tasks_executed;
  size_t total_tasks_failed;
  size_t current_active_tasks;
  size_t peak_active_tasks;
  double average_task_duration_ms;
  std::string backend_type;
  int worker_count;
  };
  
  static RuntimeStats get_stats();
  static RuntimeConfig get_config();
  
  // Backend management
  static void register_backend(const std::string& name,
  std::function<std::unique_ptr<IEventLoopBackend>()> factory);
  static std::vector<std::string> available_backends();

private:
Runtime() = delete;  // Static-only class

```
// Internal state
static std::unique_ptr<IEventLoopBackend> current_backend_;
static RuntimeConfig config_;
static std::atomic<bool> initialized_;
static std::atomic<bool> running_;
static RuntimeStats stats_;
```

};

// ============================================================================
// Backend Factory Registration
// ============================================================================

/**

- @brief Backend factory registration helper
  */
  class BackendRegistry {
  public:
  using BackendFactory = std::function<std::unique_ptr<IEventLoopBackend>()>;
  
  static void register_backend(const std::string& name, BackendFactory factory);
  static std::unique_ptr<IEventLoopBackend> create_backend(const std::string& name);
  static std::vector<std::string> list_backends();
  static bool has_backend(const std::string& name);

private:
static std::unordered_map<std::string, BackendFactory> factories_;
static std::mutex registry_mutex_;
};

// ============================================================================
// Auto-registration Helper Macro
// ============================================================================

#define STELLANE_REGISTER_BACKEND(name, class_type)   
namespace {   
struct class_type##Registrar {   
class_type##Registrar() {   
BackendRegistry::register_backend(name, []() -> std::unique_ptr<IEventLoopBackend> {   
return std::make_unique<class_type>();   
});   
}   
};   
static class_type##Registrar g_##class_type##_registrar;   
}

} // namespace stellane
