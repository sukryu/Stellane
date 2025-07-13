#include “stellane/runtime/event_loop.h”
#include “stellane/runtime/runtime_config.h”
#include “stellane/runtime/runtime_stats.h”
#include “stellane/utils/logger.h”
#include “stellane/core/task.h”

#include <algorithm>
#include <cassert>
#include <sstream>
#include <iomanip>

// Platform-specific includes
#ifdef **linux**
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#ifdef STELLANE_HAS_LIBUV
#include <uv.h>
#endif

namespace stellane {

// ============================================================================
// EventLoopStats Implementation
// ============================================================================

std::string EventLoopStats::to_string() const {
std::ostringstream oss;
oss << “Event Loop Statistics:\n”;
oss << “  Uptime: “ << uptime.count() << “ms\n”;
oss << “  Total Iterations: “ << total_iterations << “\n”;
oss << “  Total Tasks: “ << total_tasks_processed << “\n”;
oss << “  Total I/O Events: “ << total_io_events << “\n”;
oss << “  Pending Tasks: “ << current_pending_tasks << “\n”;
oss << “  Peak Pending: “ << peak_pending_tasks << “\n”;
oss << “  Avg Iteration Time: “ << std::fixed << std::setprecision(2) << average_iteration_time_us << “μs\n”;
oss << “  CPU Usage: “ << std::fixed << std::setprecision(1) << cpu_usage_percent << “%\n”;
oss << “  Tasks/Second: “ << std::fixed << std::setprecision(1) << tasks_per_second() << “\n”;
return oss.str();
}

// ============================================================================
// BaseEventLoop Implementation
// ============================================================================

BaseEventLoop::BaseEventLoop(const RuntimeConfig& config)
: config_(config)
, running_(false)
, stop_requested_(false)
, last_stats_update_(std::chrono::steady_clock::now()) {

```
initialize_logger();

// Initialize statistics
stats_.start_time = std::chrono::steady_clock::now();

logger_->info("BaseEventLoop initialized with backend: {}", to_string(config_.backend));
```

}

BaseEventLoop::~BaseEventLoop() {
if (running_.load()) {
logger_->warn(“BaseEventLoop destroyed while still running - forcing stop”);
stop();
}

```
logger_->info("BaseEventLoop destroyed");
```

}

void BaseEventLoop::initialize_logger() {
// Create or get logger for this event loop
logger_ = std::make_shared<Logger>(“stellane.event_loop”);

```
if (config_.logging.enable_trace_logging) {
    logger_->set_level(Logger::Level::TRACE);
}
```

}

// ============================================================================
// IEventLoopBackend Interface Implementation
// ============================================================================

bool BaseEventLoop::is_running() const {
return running_.load(std::memory_order_acquire);
}

size_t BaseEventLoop::pending_tasks() const {
std::shared_lock lock(task_queue_mutex_);
return task_queue_.size();
}

EventLoopStats BaseEventLoop::get_stats() const {
std::shared_lock lock(stats_mutex_);

```
// Update uptime
stats_.uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - stats_.start_time);

return stats_;
```

}

void BaseEventLoop::reset_stats() {
std::unique_lock lock(stats_mutex_);

```
auto start_time = stats_.start_time;
stats_ = EventLoopStats{};
stats_.start_time = start_time;

logger_->info("Event loop statistics reset");
```

}

void BaseEventLoop::enable_profiling(bool enabled) {
profiling_enabled_ = enabled;
logger_->info(“Event loop profiling {}”, enabled ? “enabled” : “disabled”);
}

RuntimeConfig BaseEventLoop::get_config() const {
return config_;
}

bool BaseEventLoop::update_config(const RuntimeConfig& config) {
// Update non-critical configuration that can be changed at runtime
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

logger_->info("Event loop configuration updated");
return true;
```

}

std::thread::id BaseEventLoop::get_thread_id() const {
return loop_thread_id_;
}

bool BaseEventLoop::is_in_loop_thread() const {
return std::this_thread::get_id() == loop_thread_id_;
}

// ============================================================================
// Task Scheduling Implementation
// ============================================================================

void BaseEventLoop::schedule(Task<> task) {
if (!task) {
logger_->warn(“Attempted to schedule null task”);
return;
}

```
{
    std::unique_lock lock(task_queue_mutex_);
    
    PriorityTask priority_task{
        .task = std::move(task),
        .priority = 50, // Default priority
        .created_at = std::chrono::steady_clock::now()
    };
    
    task_queue_.emplace(std::move(priority_task));
    
    // Update statistics
    {
        std::unique_lock stats_lock(stats_mutex_);
        stats_.current_pending_tasks = task_queue_.size();
        if (stats_.current_pending_tasks > stats_.peak_pending_tasks) {
            stats_.peak_pending_tasks = stats_.current_pending_tasks;
        }
    }
}

// Notify event loop of new task
task_queue_cv_.notify_one();

if (config_.logging.log_task_lifecycle) {
    logger_->trace("Task scheduled (pending: {})", pending_tasks());
}
```

}

void BaseEventLoop::schedule_with_priority(Task<> task, int priority) {
if (!task) {
logger_->warn(“Attempted to schedule null task with priority”);
return;
}

```
// Clamp priority to valid range
priority = std::clamp(priority, 0, 100);

{
    std::unique_lock lock(task_queue_mutex_);
    
    PriorityTask priority_task{
        .task = std::move(task),
        .priority = priority,
        .created_at = std::chrono::steady_clock::now()
    };
    
    task_queue_.emplace(std::move(priority_task));
    
    // Update statistics
    {
        std::unique_lock stats_lock(stats_mutex_);
        stats_.current_pending_tasks = task_queue_.size();
        if (stats_.current_pending_tasks > stats_.peak_pending_tasks) {
            stats_.peak_pending_tasks = stats_.current_pending_tasks;
        }
    }
}

task_queue_cv_.notify_one();

if (config_.logging.log_task_lifecycle) {
    logger_->trace("Task scheduled with priority {} (pending: {})", priority, pending_tasks());
}
```

}

void BaseEventLoop::post(std::function<void()> func) {
if (!func) {
logger_->warn(“Attempted to post null function”);
return;
}

```
{
    std::unique_lock lock(function_queue_mutex_);
    function_queue_.emplace(std::move(func));
}

task_queue_cv_.notify_one();

logger_->trace("Function posted to event loop");
```

}

// ============================================================================
// Timer Management Implementation
// ============================================================================

TimerHandle BaseEventLoop::create_timer(std::chrono::milliseconds delay, TimerCallback callback) {
if (!callback) {
throw TimerException(“Timer callback cannot be null”);
}

```
auto timer_id = next_timer_id_.fetch_add(1, std::memory_order_relaxed);
auto fire_time = std::chrono::steady_clock::now() + delay;

{
    std::unique_lock lock(timer_mutex_);
    
    Timer timer{
        .id = timer_id,
        .fire_time = fire_time,
        .interval = std::chrono::milliseconds::zero(), // One-shot timer
        .callback = std::move(callback),
        .active = true
    };
    
    timer_queue_.emplace(std::move(timer));
}

logger_->trace("One-shot timer created (id: {}, delay: {}ms)", timer_id, delay.count());

return TimerHandle(timer_id);
```

}

TimerHandle BaseEventLoop::create_repeating_timer(std::chrono::milliseconds interval, TimerCallback callback) {
if (!callback) {
throw TimerException(“Timer callback cannot be null”);
}

```
if (interval <= std::chrono::milliseconds::zero()) {
    throw TimerException("Timer interval must be positive");
}

auto timer_id = next_timer_id_.fetch_add(1, std::memory_order_relaxed);
auto fire_time = std::chrono::steady_clock::now() + interval;

{
    std::unique_lock lock(timer_mutex_);
    
    Timer timer{
        .id = timer_id,
        .fire_time = fire_time,
        .interval = interval,
        .callback = std::move(callback),
        .active = true
    };
    
    timer_queue_.emplace(std::move(timer));
}

logger_->trace("Repeating timer created (id: {}, interval: {}ms)", timer_id, interval.count());

return TimerHandle(timer_id);
```

}

bool BaseEventLoop::cancel_timer(const TimerHandle& handle) {
if (!handle.is_valid()) {
return false;
}

```
std::unique_lock lock(timer_mutex_);

// Find and deactivate the timer
// Note: We don't remove from queue immediately to avoid iterator invalidation
// Inactive timers are cleaned up during processing
auto temp_queue = timer_queue_;
bool found = false;

while (!temp_queue.empty()) {
    auto timer = temp_queue.top();
    temp_queue.pop();
    
    if (timer.id == handle.id() && timer.active) {
        timer.active = false;
        found = true;
    }
}

if (found) {
    logger_->trace("Timer cancelled (id: {})", handle.id());
}

return found;
```

}

TimerHandle BaseEventLoop::schedule_delayed(Task<> task, std::chrono::milliseconds delay) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
auto callback = [this, task = std::move(task)]() mutable {
    schedule(std::move(task));
};

return create_timer(delay, std::move(callback));
```

}

TimerHandle BaseEventLoop::schedule_periodic(Task<> task, std::chrono::milliseconds interval) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
// For periodic tasks, we need to create a new task each time
// Store the task in a shared_ptr so it can be copied
auto shared_task = std::make_shared<Task<>>(std::move(task));

auto callback = [this, shared_task]() {
    // Create a copy of the task for this execution
    auto task_copy = *shared_task;
    schedule(std::move(task_copy));
};

return create_repeating_timer(interval, std::move(callback));
```

}

// ============================================================================
// I/O Event Management (Base Implementation)
// ============================================================================

bool BaseEventLoop::register_io(int fd, int events, IoEventHandler handler) {
// Base implementation - should be overridden by concrete backends
logger_->warn(“register_io called on BaseEventLoop - should be implemented by concrete backend”);
return false;
}

bool BaseEventLoop::unregister_io(int fd) {
// Base implementation - should be overridden by concrete backends
logger_->warn(“unregister_io called on BaseEventLoop - should be implemented by concrete backend”);
return false;
}

bool BaseEventLoop::modify_io(int fd, int events) {
// Base implementation - should be overridden by concrete backends
logger_->warn(“modify_io called on BaseEventLoop - should be implemented by concrete backend”);
return false;
}

// ============================================================================
// Core Event Loop Processing
// ============================================================================

void BaseEventLoop::run() {
if (running_.exchange(true)) {
throw EventLoopException(“Event loop is already running”);
}

```
loop_thread_id_ = std::this_thread::get_id();
stop_requested_.store(false);

logger_->info("Event loop starting");

mark_started();

try {
    main_loop();
} catch (const std::exception& e) {
    logger_->error("Event loop error: {}", e.what());
    throw;
}

mark_stopped();

logger_->info("Event loop stopped");
```

}

void BaseEventLoop::stop() {
if (!running_.load()) {
return; // Already stopped
}

```
logger_->info("Event loop stop requested");

stop_requested_.store(true, std::memory_order_release);

// Wake up the event loop
task_queue_cv_.notify_all();

// Wait for graceful shutdown with timeout
auto timeout = std::chrono::seconds(5);
auto start_time = std::chrono::steady_clock::now();

while (running_.load() && 
       (std::chrono::steady_clock::now() - start_time) < timeout) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

if (running_.load()) {
    logger_->warn("Event loop did not stop gracefully within timeout");
    running_.store(false, std::memory_order_release);
}
```

}

void BaseEventLoop::main_loop() {
auto last_iteration_time = std::chrono::steady_clock::now();

```
while (!should_stop()) {
    auto iteration_start = std::chrono::steady_clock::now();
    
    try {
        // Process tasks
        size_t tasks_processed = process_tasks(config_.worker.max_tasks_per_loop);
        
        // Process functions
        size_t functions_processed = process_functions();
        
        // Process timers
        size_t timers_processed = process_timers();
        
        // Update statistics
        if (profiling_enabled_) {
            update_stats();
        }
        
        // Backend-specific I/O processing (virtual method)
        process_io_events();
        
        // Calculate iteration time
        auto iteration_end = std::chrono::steady_clock::now();
        auto iteration_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            iteration_end - iteration_start);
        
        // Update iteration statistics
        {
            std::unique_lock lock(stats_mutex_);
            stats_.total_iterations++;
            stats_.total_tasks_processed += tasks_processed;
            
            // Update moving average of iteration time
            double alpha = 0.1; // Smoothing factor
            stats_.average_iteration_time_us = 
                alpha * iteration_duration.count() + 
                (1.0 - alpha) * stats_.average_iteration_time_us;
        }
        
        // Idle timeout if no work was done
        if (tasks_processed == 0 && functions_processed == 0 && timers_processed == 0) {
            std::unique_lock lock(task_queue_mutex_);
            task_queue_cv_.wait_for(lock, config_.performance.idle_timeout,
                [this] { return !task_queue_.empty() || should_stop(); });
        }
        
        last_iteration_time = iteration_end;
        
    } catch (const std::exception& e) {
        logger_->error("Event loop iteration error: {}", e.what());
        
        // Continue running but log the error
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

}

size_t BaseEventLoop::process_tasks(size_t max_tasks) {
size_t processed = 0;

```
while (processed < max_tasks && !should_stop()) {
    PriorityTask priority_task;
    
    {
        std::unique_lock lock(task_queue_mutex_);
        
        if (task_queue_.empty()) {
            break;
        }
        
        priority_task = std::move(const_cast<PriorityTask&>(task_queue_.top()));
        task_queue_.pop();
        
        // Update pending task count
        {
            std::unique_lock stats_lock(stats_mutex_);
            stats_.current_pending_tasks = task_queue_.size();
        }
    }
    
    // Execute the task
    try {
        auto task_start = std::chrono::steady_clock::now();
        
        if (config_.logging.log_task_lifecycle) {
            logger_->trace("Executing task (priority: {})", priority_task.priority);
        }
        
        // Execute the coroutine task
        priority_task.task.resume();
        
        auto task_end = std::chrono::steady_clock::now();
        auto task_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            task_end - task_start).count() / 1000.0; // Convert to milliseconds
        
        // Record task execution statistics
        record_task_execution(-1, task_duration, true);
        
        processed++;
        
    } catch (const std::exception& e) {
        logger_->error("Task execution error: {}", e.what());
        
        // Record failed task
        record_task_execution(-1, 0.0, false);
        processed++;
    }
}

return processed;
```

}

size_t BaseEventLoop::process_functions() {
size_t processed = 0;

```
// Process all pending functions
while (!should_stop()) {
    std::function<void()> func;
    
    {
        std::unique_lock lock(function_queue_mutex_);
        
        if (function_queue_.empty()) {
            break;
        }
        
        func = std::move(function_queue_.front());
        function_queue_.pop();
    }
    
    try {
        func();
        processed++;
    } catch (const std::exception& e) {
        logger_->error("Function execution error: {}", e.what());
        processed++;
    }
}

return processed;
```

}

size_t BaseEventLoop::process_timers() {
size_t processed = 0;
auto now = std::chrono::steady_clock::now();

```
std::vector<Timer> expired_timers;

{
    std::unique_lock lock(timer_mutex_);
    
    // Find all expired timers
    while (!timer_queue_.empty()) {
        auto timer = timer_queue_.top();
        
        if (!timer.active) {
            // Remove inactive timer
            timer_queue_.pop();
            continue;
        }
        
        if (timer.fire_time > now) {
            // No more expired timers
            break;
        }
        
        timer_queue_.pop();
        expired_timers.push_back(std::move(timer));
    }
}

// Execute expired timers
for (auto& timer : expired_timers) {
    try {
        timer.callback();
        processed++;
        
        // If it's a repeating timer, reschedule it
        if (timer.interval > std::chrono::milliseconds::zero()) {
            timer.fire_time = now + timer.interval;
            
            std::unique_lock lock(timer_mutex_);
            timer_queue_.emplace(std::move(timer));
        }
        
    } catch (const std::exception& e) {
        logger_->error("Timer callback error: {}", e.what());
        processed++;
    }
}

return processed;
```

}

void BaseEventLoop::process_io_events() {
// Base implementation does nothing
// Concrete backends should override this method
}

// ============================================================================
// Protected Helper Methods
// ============================================================================

void BaseEventLoop::mark_started() {
std::unique_lock lock(stats_mutex_);
stats_.start_time = std::chrono::steady_clock::now();
}

void BaseEventLoop::mark_stopped() {
running_.store(false, std::memory_order_release);
}

bool BaseEventLoop::should_stop() const {
return stop_requested_.load(std::memory_order_acquire);
}

void BaseEventLoop::update_stats() {
auto now = std::chrono::steady_clock::now();

```
// Update stats every 1 second
if (now - last_stats_update_ < std::chrono::seconds(1)) {
    return;
}

std::unique_lock lock(stats_mutex_);

// Update uptime
stats_.uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
    now - stats_.start_time);

// Update CPU usage (simplified estimation)
// In a real implementation, this would use platform-specific APIs
stats_.cpu_usage_percent = 50.0; // Placeholder

last_stats_update_ = now;
```

}

void BaseEventLoop::cleanup_expired_timers() {
std::unique_lock lock(timer_mutex_);

```
// Rebuild timer queue without inactive timers
std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> new_queue;

while (!timer_queue_.empty()) {
    auto timer = timer_queue_.top();
    timer_queue_.pop();
    
    if (timer.active) {
        new_queue.emplace(std::move(timer));
    }
}

timer_queue_ = std::move(new_queue);
```

}

// ============================================================================
// Event Loop Factory Implementation
// ============================================================================

std::unordered_map<std::string, EventLoopFactory::BackendFactory> EventLoopFactory::factories_;
std::mutex EventLoopFactory::factories_mutex_;

std::unique_ptr<IEventLoopBackend> EventLoopFactory::create(RuntimeBackend backend, const RuntimeConfig& config) {
switch (backend) {
case RuntimeBackend::LIBUV:
#ifdef STELLANE_HAS_LIBUV
return create_libuv_backend(config);
#else
throw EventLoopException(“libuv backend not available - not compiled with libuv support”);
#endif

```
    case RuntimeBackend::EPOLL:
```

#ifdef **linux**
return create_epoll_backend(config);
#else
throw EventLoopException(“epoll backend not available - only supported on Linux”);
#endif

```
    case RuntimeBackend::IO_URING:
```

#ifdef **linux**
return create_io_uring_backend(config);
#else
throw EventLoopException(“io_uring backend not available - only supported on Linux”);
#endif

```
    case RuntimeBackend::STELLANE:
        return create_stellane_backend(config);
        
    case RuntimeBackend::CUSTOM:
        if (config.custom_backend_factory) {
            return config.custom_backend_factory();
        } else {
            throw EventLoopException("Custom backend factory not provided");
        }
        
    default:
        throw EventLoopException("Unknown backend type: " + std::to_string(static_cast<int>(backend)));
}
```

}

void EventLoopFactory::register_backend(const std::string& name, BackendFactory factory) {
std::unique_lock lock(factories_mutex_);
factories_[name] = std::move(factory);
}

std::vector<std::string> EventLoopFactory::get_available_backends() {
std::vector<std::string> available;

#ifdef STELLANE_HAS_LIBUV
available.push_back(“libuv”);
#endif

#ifdef **linux**
available.push_back(“epoll”);
available.push_back(“io_uring”);
#endif

```
available.push_back("stellane");

// Add custom registered backends
std::shared_lock lock(factories_mutex_);
for (const auto& [name, factory] : factories_) {
    available.push_back(name);
}

return available;
```

}

bool EventLoopFactory::is_backend_available(RuntimeBackend backend) {
try {
auto available_backends = get_available_backends();
std::string backend_name = to_string(backend);

```
    return std::find(available_backends.begin(), available_backends.end(), backend_name) 
           != available_backends.end();
} catch (...) {
    return false;
}
```

}

RuntimeBackend EventLoopFactory::get_recommended_backend() {
#ifdef **linux**
// On Linux, prefer io_uring > epoll > libuv
if (is_backend_available(RuntimeBackend::IO_URING)) {
return RuntimeBackend::IO_URING;
}
if (is_backend_available(RuntimeBackend::EPOLL)) {
return RuntimeBackend::EPOLL;
}
#endif

#ifdef STELLANE_HAS_LIBUV
return RuntimeBackend::LIBUV;
#endif

```
// Fallback to custom Stellane backend
return RuntimeBackend::STELLANE;
```

}

// ============================================================================
// Backend Creation Functions (Forward Declarations)
// ============================================================================

std::unique_ptr<IEventLoopBackend> create_stellane_backend(const RuntimeConfig& config) {
// For now, return a BaseEventLoop instance
// TODO: Implement actual StellaneBackend
return std::make_unique<BaseEventLoop>(config);
}

#ifdef STELLANE_HAS_LIBUV
std::unique_ptr<IEventLoopBackend> create_libuv_backend(const RuntimeConfig& config) {
// TODO: Implement LibUVBackend
throw EventLoopException(“LibUV backend not yet implemented”);
}
#endif

#ifdef **linux**
std::unique_ptr<IEventLoopBackend> create_epoll_backend(const RuntimeConfig& config) {
// TODO: Implement EpollBackend
throw EventLoopException(“Epoll backend not yet implemented”);
}

std::unique_ptr<IEventLoopBackend> create_io_uring_backend(const RuntimeConfig& config) {
// TODO: Implement IoUringBackend
throw EventLoopException(“io_uring backend not yet implemented”);
}
#endif

} // namespace stellane
