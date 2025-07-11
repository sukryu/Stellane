# Runtime API Reference

## Overview

The Stellane Runtime API provides comprehensive control over the asynchronous execution environment that powers high-performance web servers. This reference documents the public interfaces, backend modules, task scheduling policies, and recovery hook systems available to enterprise developers and infrastructure architects.

### Design Goals

- **Non-blocking Request Processing**: All operations execute asynchronously without blocking the event loop
- **Cross-platform Event Loop Abstraction**: Unified interface across different operating systems and I/O models
- **Lightweight Coroutine Execution**: Task-based execution model with minimal overhead
- **Fault-tolerant Request Recovery**: Production-ready mechanisms for handling server failures

## Core Components

### IEventLoopBackend

The foundational interface for all event loop implementations in Stellane.

```cpp
class IEventLoopBackend {
public:
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void schedule(Task<> task) = 0;
    virtual bool is_running() const = 0;
    virtual size_t pending_tasks() const = 0;
    virtual ~IEventLoopBackend() = default;
};
```

#### Methods

|Method            |Description                               |Thread Safety  |
|------------------|------------------------------------------|---------------|
|`run()`           |Starts the event loop (blocking operation)|Not thread-safe|
|`stop()`          |Gracefully shuts down the event loop      |Thread-safe    |
|`schedule(Task<>)`|Enqueues a task for execution             |Thread-safe    |
|`is_running()`    |Returns current loop execution state      |Thread-safe    |
|`pending_tasks()` |Returns number of queued tasks            |Thread-safe    |

#### Implementation Requirements

Custom backends must ensure:

- **Thread Safety**: `schedule()`, `stop()`, `is_running()`, and `pending_tasks()` must be thread-safe
- **Graceful Shutdown**: `stop()` should complete pending tasks before termination
- **Exception Safety**: Exceptions in tasks must not crash the event loop
- **Resource Cleanup**: Proper cleanup of file descriptors and memory

### RuntimeConfig

Configuration structure for runtime initialization.

```cpp
struct RuntimeConfig {
    std::string backend_type = "libuv";
    int worker_threads = std::thread::hardware_concurrency();
    int max_tasks_per_loop = 1000;
    bool enable_cpu_affinity = false;
    bool enable_numa_awareness = false;
    TaskSchedulingStrategy strategy = TaskSchedulingStrategy::FIFO;
    
    // Recovery settings
    bool enable_request_recovery = false;
    std::string recovery_backend = "mmap";
    std::string recovery_path = "/tmp/stellane_recovery";
    int max_recovery_attempts = 3;
    
    // Performance tuning
    bool zero_copy_io = true;
    int io_batch_size = 32;
    std::chrono::milliseconds idle_timeout{100};
};
```

#### Configuration File Format

```toml
[runtime]
backend = "io_uring"
worker_threads = 8
max_tasks_per_loop = 500
enable_cpu_affinity = true
enable_numa_awareness = true
strategy = "work_stealing"

[runtime.recovery]
enabled = true
backend = "rocksdb"
path = "/var/log/stellane/recovery"
max_attempts = 5

[runtime.performance]
zero_copy_io = true
io_batch_size = 64
idle_timeout = "50ms"
```

### Runtime

The primary runtime engine managing task execution and event loop coordination.

```cpp
class Runtime {
public:
    // Initialization
    static void init(const RuntimeConfig& config);
    static void init_from_config_file(const std::string& path);
    
    // Lifecycle management
    static void start();
    static void stop();
    static void restart();
    
    // Task scheduling
    static void schedule(Task<> task);
    static void schedule_with_priority(Task<> task, int priority);
    static void schedule_on_worker(Task<> task, int worker_id);
    
    // Monitoring
    static RuntimeStats get_stats();
    static std::vector<WorkerStats> get_worker_stats();
    
    // Configuration
    static RuntimeConfig get_config();
    static void update_config(const RuntimeConfig& config);
    
private:
    Runtime() = delete;
};
```

#### Thread Safety Guarantees

- **Static Methods**: All public static methods are thread-safe
- **Configuration Updates**: Config changes apply to new tasks only
- **Graceful Shutdown**: `stop()` waits for task completion with configurable timeout

## Task Scheduling

### Task<T>

The fundamental unit of asynchronous work in Stellane.

```cpp
template<typename T = void>
class Task {
public:
    Task() = default;
    Task(const Task&) = delete;
    Task(Task&&) = default;
    
    // Awaitable interface
    bool await_ready() const;
    void await_suspend(std::coroutine_handle<> handle);
    T await_resume();
    
    // Task metadata
    std::string get_name() const;
    int get_priority() const;
    std::chrono::steady_clock::time_point get_created_at() const;
};
```

#### Usage Examples

```cpp
// Simple async operation
Task<Response> handle_request(Context& ctx, const Request& req) {
    auto user_id = req.param<int>("user_id");
    auto user = co_await database.fetch_user(user_id);
    co_return Response::json(user);
}

// Error handling
Task<Response> safe_handler(Context& ctx, const Request& req) {
    try {
        auto result = co_await risky_operation();
        co_return Response::ok(result);
    } catch (const std::exception& e) {
        ctx.logger().error("Operation failed: {}", e.what());
        co_return Response::internal_error();
    }
}

// Task composition
Task<UserProfile> get_user_profile(int user_id) {
    auto user = co_await database.fetch_user(user_id);
    auto settings = co_await database.fetch_settings(user_id);
    auto preferences = co_await cache.get_preferences(user_id);
    
    co_return UserProfile{
        .user = user,
        .settings = settings,
        .preferences = preferences
    };
}
```

### Scheduling Strategies

```cpp
enum class TaskSchedulingStrategy {
    FIFO,           // First-in, first-out
    PRIORITY,       // Priority-based scheduling
    AFFINITY,       // CPU/worker affinity
    WORK_STEALING,  // Dynamic load balancing
    CUSTOM          // User-defined scheduler
};
```

#### Strategy Characteristics

|Strategy         |Latency |Throughput|CPU Usage|Use Case               |
|-----------------|--------|----------|---------|-----------------------|
|**FIFO**         |Low     |Medium    |Low      |General purpose        |
|**PRIORITY**     |Variable|Medium    |Medium   |Mixed workloads        |
|**AFFINITY**     |Low     |High      |Medium   |Connection-bound tasks |
|**WORK_STEALING**|Medium  |High      |High     |CPU-intensive workloads|

### Custom Schedulers

```cpp
class ITaskScheduler {
public:
    virtual void enqueue(Task<> task) = 0;
    virtual Task<> dequeue() = 0;
    virtual bool has_tasks() const = 0;
    virtual size_t size() const = 0;
    virtual ~ITaskScheduler() = default;
};

// Registration
Runtime::register_scheduler("custom", std::make_unique<MyScheduler>());
```

## Recovery System

### Enabling Request Recovery

```cpp
// Programmatic configuration
RuntimeConfig config;
config.enable_request_recovery = true;
config.recovery_backend = "rocksdb";
config.recovery_path = "/persistent/recovery";
Runtime::init(config);

// Or via configuration file
server.enable_request_recovery();
```

### Recovery Hooks

```cpp
// Basic recovery handler
server.on_recover([](Context& ctx, const Request& req) -> Task<> {
    ctx.logger().info("Recovering request: {} {}", req.method(), req.path());
    
    // Custom recovery logic
    if (req.method() == "POST") {
        co_await handle_post_recovery(req);
    }
    
    // Retry original handler
    co_await retry_original_handler(req);
});

// Advanced recovery with metadata
server.on_recover([](Context& ctx, const Request& req, const RecoveryMetadata& meta) -> Task<> {
    ctx.logger().info("Recovering request {} (attempt {}/{})", 
        meta.request_id, meta.attempt, meta.max_attempts);
    
    // Check if this is a retry
    if (meta.attempt > 1) {
        // Implement exponential backoff
        auto delay = std::chrono::milliseconds(100 * (1 << (meta.attempt - 1)));
        co_await sleep_for(delay);
    }
    
    // Conditional recovery based on request type
    switch (req.method()) {
        case HttpMethod::GET:
            co_await handle_get_recovery(req);
            break;
        case HttpMethod::POST:
            co_await handle_post_recovery(req, meta);
            break;
        default:
            ctx.logger().warn("Unknown method in recovery: {}", req.method());
            break;
    }
});
```

### Recovery Metadata

```cpp
struct RecoveryMetadata {
    std::string request_id;
    std::chrono::system_clock::time_point original_timestamp;
    std::chrono::system_clock::time_point recovery_timestamp;
    int attempt;
    int max_attempts;
    std::string failure_reason;
    std::map<std::string, std::string> custom_data;
};
```

## Backend Registration

### Runtime Factory

```cpp
class RuntimeFactory {
public:
    using BackendBuilder = std::function<std::unique_ptr<IEventLoopBackend>()>;
    
    static void register_backend(const std::string& name, BackendBuilder builder);
    static std::unique_ptr<IEventLoopBackend> create_backend(const std::string& name);
    static std::vector<std::string> list_backends();
    static bool has_backend(const std::string& name);
};
```

#### Custom Backend Implementation

```cpp
class MyCustomEventLoop : public IEventLoopBackend {
private:
    std::atomic<bool> running_{false};
    std::queue<Task<>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
public:
    void run() override {
        running_ = true;
        
        while (running_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !task_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            auto task = std::move(task_queue_.front());
            task_queue_.pop();
            lock.unlock();
            
            // Execute task
            execute_task(std::move(task));
        }
    }
    
    void stop() override {
        running_ = false;
        queue_cv_.notify_all();
    }
    
    void schedule(Task<> task) override {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
        queue_cv_.notify_one();
    }
    
    bool is_running() const override {
        return running_;
    }
    
    size_t pending_tasks() const override {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return task_queue_.size();
    }
};

// Registration
RuntimeFactory::register_backend("custom", [] {
    return std::make_unique<MyCustomEventLoop>();
});
```

## Monitoring & Observability

### Runtime Statistics

```cpp
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

struct WorkerStats {
    int worker_id;
    size_t tasks_executed;
    size_t tasks_failed;
    size_t current_queue_size;
    size_t peak_queue_size;
    double cpu_usage_percent;
    std::thread::id thread_id;
};
```

### Metrics Collection

```cpp
// Get runtime metrics
auto stats = Runtime::get_stats();
std::cout << "Uptime: " << stats.uptime.count() << "ms\n";
std::cout << "Tasks executed: " << stats.total_tasks_executed << "\n";
std::cout << "Average task duration: " << stats.average_task_duration_ms << "ms\n";

// Per-worker metrics
auto worker_stats = Runtime::get_worker_stats();
for (const auto& worker : worker_stats) {
    std::cout << "Worker " << worker.worker_id 
              << ": " << worker.tasks_executed << " tasks, "
              << worker.cpu_usage_percent << "% CPU\n";
}
```

### Performance Profiling

```cpp
// Enable detailed profiling
Runtime::enable_profiling(true);

// Custom metrics
Runtime::add_metric("custom_operation_count", 42);
Runtime::increment_metric("cache_hits");
Runtime::set_gauge("active_connections", connection_count);
```

## Configuration Management

### Dynamic Configuration Updates

```cpp
// Update runtime configuration
RuntimeConfig new_config = Runtime::get_config();
new_config.worker_threads = 12;
new_config.max_tasks_per_loop = 2000;
Runtime::update_config(new_config);

// Configuration validation
bool is_valid = Runtime::validate_config(new_config);
if (!is_valid) {
    std::cerr << "Invalid configuration\n";
}
```

### Environment Variable Support

```cpp
// Environment variable overrides
// STELLANE_RUNTIME_BACKEND=io_uring
// STELLANE_RUNTIME_WORKERS=16
// STELLANE_RUNTIME_RECOVERY_ENABLED=true

RuntimeConfig config = RuntimeConfig::from_environment();
```

## Complete Example

```cpp
#include <stellane/runtime.hpp>
#include <stellane/server.hpp>

int main() {
    // Configure runtime
    RuntimeConfig config;
    config.backend_type = "io_uring";
    config.worker_threads = 8;
    config.enable_cpu_affinity = true;
    config.enable_request_recovery = true;
    config.strategy = TaskSchedulingStrategy::WORK_STEALING;
    
    // Initialize runtime
    Runtime::init(config);
    
    // Create server
    Server server;
    
    // Enable recovery
    server.on_recover([](Context& ctx, const Request& req) -> Task<> {
        ctx.logger().info("Recovering request: {}", req.path());
        co_await retry_original_handler(req);
    });
    
    // Define routes
    server.get("/users/{id}", [](Context& ctx) -> Task<Response> {
        auto user_id = ctx.param<int>("id");
        auto user = co_await database.fetch_user(user_id);
        co_return Response::json(user);
    });
    
    server.post("/users", [](Context& ctx) -> Task<Response> {
        auto user_data = co_await ctx.request().json<UserData>();
        auto user = co_await database.create_user(user_data);
        co_return Response::json(user).status(201);
    });
    
    // Start server
    server.listen(8080);
    
    // Start runtime (blocks until shutdown)
    Runtime::start();
    
    return 0;
}
```

## Error Handling

### Exception Safety

```cpp
// Runtime exceptions
class RuntimeException : public std::exception {
public:
    RuntimeException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }
private:
    std::string message_;
};

class TaskExecutionException : public RuntimeException {
public:
    TaskExecutionException(const std::string& task_name, const std::string& error)
        : RuntimeException("Task '" + task_name + "' failed: " + error) {}
};

class BackendException : public RuntimeException {
public:
    BackendException(const std::string& backend, const std::string& error)
        : RuntimeException("Backend '" + backend + "' error: " + error) {}
};
```

### Error Handling Patterns

```cpp
// Graceful error handling in tasks
Task<Response> safe_database_operation(Context& ctx, int user_id) {
    try {
        auto user = co_await database.fetch_user(user_id);
        co_return Response::json(user);
    } catch (const DatabaseException& e) {
        ctx.logger().error("Database error: {}", e.what());
        co_return Response::internal_error("Database temporarily unavailable");
    } catch (const std::exception& e) {
        ctx.logger().error("Unexpected error: {}", e.what());
        co_return Response::internal_error("Internal server error");
    }
}

// Runtime error handling
server.on_task_error([](const std::exception& e, const Task<>& task) {
    std::cerr << "Task execution failed: " << e.what() << std::endl;
    // Optional: send to error reporting service
});
```

## Future Roadmap

### Planned Features

#### Q3 2025

- [ ] **Advanced Monitoring**: Real-time task execution visualization
- [ ] **Dynamic Worker Scaling**: Automatic worker thread adjustment
- [ ] **Distributed Runtime**: Multi-process task distribution
- [ ] **Custom Allocators**: Memory pool optimization for tasks

#### Q4 2025

- [ ] **WebAssembly Backend**: WASM-based task execution
- [ ] **GPU Acceleration**: CUDA/OpenCL task offloading
- [ ] **Network Topology Awareness**: NUMA-optimized scheduling
- [ ] **Serverless Integration**: AWS Lambda/Azure Functions compatibility

#### Q1 2026

- [ ] **ML-based Scheduling**: Predictive task scheduling
- [ ] **Real-time Analytics**: Built-in APM and tracing
- [ ] **Configuration Hot-reload**: Zero-downtime config updates
- [ ] **Declarative Pipelines**: YAML-based task orchestration

### API Stability

- **Stable APIs**: Core runtime interfaces (guaranteed backward compatibility)
- **Experimental APIs**: Marked with `[[experimental]]` attribute
- **Deprecated APIs**: Marked with `[[deprecated]]` and removal timeline

## Best Practices

### Performance Optimization

1. **Task Granularity**: Keep tasks focused and lightweight
1. **Avoid Blocking**: Never use blocking I/O in tasks
1. **Resource Management**: Use RAII for automatic cleanup
1. **Batch Operations**: Group related operations when possible

### Error Handling

1. **Fail Fast**: Validate inputs early in request processing
1. **Graceful Degradation**: Provide fallback mechanisms
1. **Error Propagation**: Use structured error types
1. **Recovery Testing**: Regularly test recovery scenarios

### Monitoring

1. **Metrics Collection**: Track key performance indicators
1. **Alerting**: Set up alerts for critical thresholds
1. **Logging**: Use structured logging with correlation IDs
1. **Profiling**: Regular performance profiling in production

## Related Documentation

- [Async Runtime Architecture](../internals/async_runtime.md) - Internal implementation details
- [Context System](../concepts/context.md) - Request context propagation
- [Server Configuration](../reference/server.md) - Server setup and configuration
- [Performance Tuning](../guides/performance.md) - Optimization strategies
- [Deployment Guide](../guides/deployment.md) - Production deployment best practices
