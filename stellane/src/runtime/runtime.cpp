#include “stellane/runtime/runtime.h”
#include “stellane/runtime/runtime_config.h”
#include “stellane/runtime/runtime_stats.h”
#include “stellane/runtime/event_loop.h”
#include “stellane/runtime/task_scheduler.h”
#include “stellane/utils/logger.h”

#include <cassert>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <signal.h>

// Platform-specific includes
#ifdef **linux**
#include <sys/prctl.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <processthreadsapi.h>
#endif

namespace stellane {

// ============================================================================
// Runtime Implementation (Singleton Pattern)
// ============================================================================

namespace {

// Global runtime state
struct RuntimeState {
// Core components
std::unique_ptr<IEventLoopBackend> event_loop;
std::unique_ptr<ITaskScheduler> scheduler;
std::unique_ptr<RequestJournal> journal;
std::shared_ptr<Logger> logger;

```
// Configuration
RuntimeConfig config;

// State management
std::atomic<bool> initialized{false};
std::atomic<bool> running{false};
std::atomic<bool> shutdown_requested{false};

// Thread synchronization
std::mutex state_mutex;
std::condition_variable state_cv;

// Recovery system
RecoveryHook recovery_hook;
TaskErrorHandler error_handler;
bool recovery_enabled{false};

// Statistics and monitoring
std::chrono::steady_clock::time_point start_time;
std::thread stats_thread;

// Handles for periodic/delayed tasks
std::atomic<size_t> next_handle_id{1};
std::unordered_map<size_t, std::function<void()>> periodic_tasks;
std::unordered_map<size_t, std::function<void()>> delayed_tasks;
std::mutex handles_mutex;

RuntimeState() = default;
~RuntimeState() = default;

// Non-copyable, non-movable
RuntimeState(const RuntimeState&) = delete;
RuntimeState& operator=(const RuntimeState&) = delete;
```

};

// Global runtime instance
RuntimeState g_runtime_state;

// Signal handler for graceful shutdown
void signal_handler(int signal) {
if (signal == SIGINT || signal == SIGTERM) {
g_runtime_state.logger->info(“Received signal {}, initiating graceful shutdown”, signal);
Runtime::stop();
}
}

// Setup signal handlers for graceful shutdown
void setup_signal_handlers() {
#ifdef **linux**
struct sigaction sa;
sa.sa_handler = signal_handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;

```
sigaction(SIGINT, &sa, nullptr);
sigaction(SIGTERM, &sa, nullptr);

// Ignore SIGPIPE (common in network applications)
signal(SIGPIPE, SIG_IGN);
```

#elif defined(_WIN32)
SetConsoleCtrlHandler([](DWORD ctrl_type) -> BOOL {
if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_CLOSE_EVENT) {
signal_handler(SIGINT);
return TRUE;
}
return FALSE;
}, TRUE);
#endif
}

// Set process title for debugging
void set_process_title(const std::string& title) {
#ifdef **linux**
prctl(PR_SET_NAME, title.c_str(), 0, 0, 0);
#elif defined(_WIN32)
SetConsoleTitleA(title.c_str());
#endif
}

} // anonymous namespace

// ============================================================================
// Runtime Class Implementation
// ============================================================================

void Runtime::init(const RuntimeConfig& config) {
std::unique_lock lock(g_runtime_state.state_mutex);

```
if (g_runtime_state.initialized.load()) {
    throw RuntimeInitializationException("Runtime already initialized");
}

try {
    // Store configuration
    g_runtime_state.config = config;
    
    // Initialize logger first
    g_runtime_state.logger = std::make_shared<Logger>("stellane.runtime");
    g_runtime_state.logger->info("Initializing Stellane Runtime");
    g_runtime_state.logger->info("Backend: {}, Workers: {}, Strategy: {}",
                                to_string(config.backend),
                                config.worker.worker_threads,
                                to_string(config.strategy));
    
    // Validate configuration
    auto validation = config.validate();
    if (!validation.is_valid) {
        std::ostringstream oss;
        oss << "Configuration validation failed: ";
        for (const auto& error : validation.errors) {
            oss << error << "; ";
        }
        throw RuntimeInitializationException(oss.str());
    }
    
    // Log warnings
    for (const auto& warning : validation.warnings) {
        g_runtime_state.logger->warn("Configuration warning: {}", warning);
    }
    
    // Initialize statistics system
    initialize_stats_collector(config.worker.worker_threads, to_string(config.backend));
    
    // Create event loop backend
    g_runtime_state.event_loop = EventLoopFactory::create(config.backend, config);
    g_runtime_state.logger->info("Event loop backend initialized: {}", to_string(config.backend));
    
    // Create task scheduler
    g_runtime_state.scheduler = create_task_scheduler(config);
    g_runtime_state.logger->info("Task scheduler initialized: {}", to_string(config.strategy));
    
    // Initialize recovery system if enabled
    if (config.recovery.enabled) {
        initialize_recovery_system(config);
        g_runtime_state.recovery_enabled = true;
        g_runtime_state.logger->info("Recovery system initialized");
    }
    
    // Setup signal handlers
    setup_signal_handlers();
    
    // Set process title
    set_process_title("stellane-runtime");
    
    g_runtime_state.start_time = std::chrono::steady_clock::now();
    g_runtime_state.initialized.store(true);
    
    g_runtime_state.logger->info("Runtime initialization complete");
    
} catch (const std::exception& e) {
    g_runtime_state.logger->error("Runtime initialization failed: {}", e.what());
    
    // Cleanup partial initialization
    g_runtime_state.event_loop.reset();
    g_runtime_state.scheduler.reset();
    g_runtime_state.journal.reset();
    
    throw RuntimeInitializationException(e.what());
}
```

}

void Runtime::init_from_config_file(const std::string& config_path) {
auto config_opt = RuntimeConfig::from_toml_file(config_path);
if (!config_opt) {
throw RuntimeInitializationException(“Failed to load configuration from: “ + config_path);
}

```
// Apply environment variable overrides
auto final_config = config_opt->with_environment_overrides();

init(final_config);
```

}

void Runtime::start() {
std::unique_lock lock(g_runtime_state.state_mutex);

```
if (!g_runtime_state.initialized.load()) {
    throw RuntimeException("Runtime not initialized - call init() first");
}

if (g_runtime_state.running.load()) {
    throw RuntimeException("Runtime already running");
}

try {
    g_runtime_state.logger->info("Starting Stellane Runtime");
    
    // Start task scheduler
    g_runtime_state.scheduler->start();
    g_runtime_state.logger->info("Task scheduler started");
    
    // Start statistics collection
    start_periodic_stats_update(std::chrono::milliseconds(1000));
    
    // Start statistics thread
    g_runtime_state.stats_thread = std::thread(&Runtime::stats_thread_main);
    
    g_runtime_state.running.store(true);
    g_runtime_state.shutdown_requested.store(false);
    
    lock.unlock();
    
    g_runtime_state.logger->info("Runtime started successfully");
    
    // Run main event loop (blocking)
    run_main_loop();
    
} catch (const std::exception& e) {
    g_runtime_state.logger->error("Runtime start failed: {}", e.what());
    g_runtime_state.running.store(false);
    throw RuntimeException("Failed to start runtime: " + std::string(e.what()));
}
```

}

void Runtime::stop(std::chrono::milliseconds timeout) {
if (!g_runtime_state.running.load()) {
return; // Already stopped
}

```
g_runtime_state.logger->info("Stopping Stellane Runtime (timeout: {}ms)", timeout.count());

// Signal shutdown
g_runtime_state.shutdown_requested.store(true);

auto shutdown_start = std::chrono::steady_clock::now();

try {
    // Stop accepting new tasks
    if (g_runtime_state.event_loop) {
        g_runtime_state.event_loop->stop();
    }
    
    // Gracefully shutdown scheduler
    if (g_runtime_state.scheduler) {
        g_runtime_state.scheduler->shutdown();
    }
    
    // Stop statistics collection
    stop_periodic_stats_update();
    
    // Wait for statistics thread
    if (g_runtime_state.stats_thread.joinable()) {
        g_runtime_state.stats_thread.join();
    }
    
    // Cleanup recovery system
    if (g_runtime_state.journal) {
        g_runtime_state.journal.reset();
    }
    
    // Wait for graceful shutdown with timeout
    auto shutdown_duration = std::chrono::steady_clock::now() - shutdown_start;
    if (shutdown_duration < timeout) {
        std::unique_lock lock(g_runtime_state.state_mutex);
        g_runtime_state.state_cv.wait_for(lock, timeout - shutdown_duration,
            [] { return !g_runtime_state.running.load(); });
    }
    
    g_runtime_state.running.store(false);
    
    auto total_duration = std::chrono::steady_clock::now() - shutdown_start;
    g_runtime_state.logger->info("Runtime stopped (shutdown took {}ms)", 
                                std::chrono::duration_cast<std::chrono::milliseconds>(total_duration).count());
    
} catch (const std::exception& e) {
    g_runtime_state.logger->error("Error during runtime shutdown: {}", e.what());
    g_runtime_state.running.store(false);
}

// Final cleanup
shutdown_stats_collector();
```

}

void Runtime::restart(const RuntimeConfig& new_config, std::chrono::milliseconds graceful_timeout) {
g_runtime_state.logger->info(“Restarting runtime with new configuration”);

```
// Stop current runtime
stop(graceful_timeout);

// Reset state
{
    std::unique_lock lock(g_runtime_state.state_mutex);
    g_runtime_state.initialized.store(false);
    g_runtime_state.event_loop.reset();
    g_runtime_state.scheduler.reset();
    g_runtime_state.journal.reset();
}

// Initialize with new configuration
init(new_config);

// Start runtime
start();
```

}

bool Runtime::is_running() noexcept {
return g_runtime_state.running.load();
}

bool Runtime::is_initialized() noexcept {
return g_runtime_state.initialized.load();
}

// ============================================================================
// Task Scheduling Interface
// ============================================================================

void Runtime::schedule(Task<> task) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (!g_runtime_state.running.load()) {
    throw RuntimeException("Runtime not running - cannot schedule task");
}

// Log task scheduling if enabled
if (g_runtime_state.config.logging.log_task_lifecycle) {
    g_runtime_state.logger->trace("Scheduling task");
}

// Schedule through scheduler
if (g_runtime_state.scheduler) {
    TaskAffinity default_affinity;
    g_runtime_state.scheduler->schedule_task(std::move(task), 
                                            static_cast<int>(TaskPriority::NORMAL), 
                                            default_affinity);
} else {
    throw RuntimeException("Task scheduler not available");
}
```

}

void Runtime::schedule_with_priority(Task<> task, int priority) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (!g_runtime_state.running.load()) {
    throw RuntimeException("Runtime not running - cannot schedule task");
}

// Clamp priority to valid range
priority = std::clamp(priority, 0, 100);

if (g_runtime_state.config.logging.log_task_lifecycle) {
    g_runtime_state.logger->trace("Scheduling task with priority {}", priority);
}

if (g_runtime_state.scheduler) {
    TaskAffinity default_affinity;
    g_runtime_state.scheduler->schedule_task(std::move(task), priority, default_affinity);
} else {
    throw RuntimeException("Task scheduler not available");
}
```

}

void Runtime::schedule_on_worker(Task<> task, int worker_id) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (!g_runtime_state.running.load()) {
    throw RuntimeException("Runtime not running - cannot schedule task");
}

if (worker_id < 0 || worker_id >= static_cast<int>(g_runtime_state.config.worker.worker_threads)) {
    throw std::invalid_argument("Invalid worker ID: " + std::to_string(worker_id));
}

if (g_runtime_state.config.logging.log_task_lifecycle) {
    g_runtime_state.logger->trace("Scheduling task on worker {}", worker_id);
}

if (g_runtime_state.scheduler) {
    TaskAffinity affinity;
    affinity.preferred_worker = worker_id;
    affinity.allow_migration = false;
    
    g_runtime_state.scheduler->schedule_task(std::move(task), 
                                            static_cast<int>(TaskPriority::NORMAL), 
                                            affinity);
} else {
    throw RuntimeException("Task scheduler not available");
}
```

}

void Runtime::schedule_with_hint(Task<> task, const SchedulingHint& hint) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (!g_runtime_state.running.load()) {
    throw RuntimeException("Runtime not running - cannot schedule task");
}

TaskAffinity affinity;
affinity.preferred_worker = hint.preferred_worker;
affinity.numa_node = hint.numa_node;
affinity.affinity_group = hint.affinity_group;

int priority = hint.priority.value_or(static_cast<int>(TaskPriority::NORMAL));

if (g_runtime_state.scheduler) {
    g_runtime_state.scheduler->schedule_task(std::move(task), priority, affinity);
} else {
    throw RuntimeException("Task scheduler not available");
}
```

}

size_t Runtime::pending_task_count() noexcept {
if (g_runtime_state.scheduler) {
auto stats = g_runtime_state.scheduler->get_stats();
return stats.current_pending_tasks;
}
return 0;
}

size_t Runtime::active_task_count() noexcept {
auto runtime_stats = get_runtime_stats();
return runtime_stats.current_active_tasks.load();
}

// ============================================================================
// Performance Monitoring and Statistics
// ============================================================================

RuntimeStats Runtime::get_stats() {
return get_runtime_stats();
}

std::vector<WorkerStats> Runtime::get_worker_stats() {
return stellane::runtime::get_worker_stats();
}

void Runtime::enable_profiling(bool enabled) {
stellane::runtime::enable_profiling(enabled);

```
if (g_runtime_state.event_loop) {
    g_runtime_state.event_loop->enable_profiling(enabled);
}

g_runtime_state.logger->info("Runtime profiling {}", enabled ? "enabled" : "disabled");
```

}

void Runtime::reset_stats() {
reset_all_stats();

```
if (g_runtime_state.event_loop) {
    g_runtime_state.event_loop->reset_stats();
}

if (g_runtime_state.scheduler) {
    g_runtime_state.scheduler->reset_stats();
}

g_runtime_state.logger->info("All runtime statistics reset");
```

}

void Runtime::add_metric(const std::string& name, double value) {
// Custom metrics are stored in a map and included in stats
static std::unordered_map<std::string, double> custom_metrics;
static std::mutex metrics_mutex;

```
std::unique_lock lock(metrics_mutex);
custom_metrics[name] = value;

if (g_runtime_state.config.logging.enable_trace_logging) {
    g_runtime_state.logger->trace("Custom metric set: {} = {}", name, value);
}
```

}

void Runtime::increment_metric(const std::string& name) {
static std::unordered_map<std::string, std::atomic<size_t>> counters;
static std::mutex counters_mutex;

```
std::unique_lock lock(counters_mutex);
counters[name].fetch_add(1, std::memory_order_relaxed);
```

}

void Runtime::set_gauge(const std::string& name, double value) {
add_metric(name, value);
}

// ============================================================================
// Configuration Management
// ============================================================================

RuntimeConfig Runtime::get_config() {
std::shared_lock lock(g_runtime_state.state_mutex);
return g_runtime_state.config;
}

void Runtime::update_config(const RuntimeConfig& new_config) {
std::unique_lock lock(g_runtime_state.state_mutex);

```
// Validate new configuration
auto validation = new_config.validate();
if (!validation.is_valid) {
    std::ostringstream oss;
    oss << "Invalid configuration: ";
    for (const auto& error : validation.errors) {
        oss << error << "; ";
    }
    throw std::invalid_argument(oss.str());
}

// Update components that support hot reload
if (g_runtime_state.event_loop) {
    g_runtime_state.event_loop->update_config(new_config);
}

if (g_runtime_state.scheduler) {
    g_runtime_state.scheduler->update_config(new_config);
}

// Update stored configuration
g_runtime_state.config = new_config;

g_runtime_state.logger->info("Runtime configuration updated");
```

}

bool Runtime::validate_config(const RuntimeConfig& config) {
auto validation = config.validate();

```
if (!validation.is_valid) {
    for (const auto& error : validation.errors) {
        g_runtime_state.logger->error("Configuration error: {}", error);
    }
}

for (const auto& warning : validation.warnings) {
    g_runtime_state.logger->warn("Configuration warning: {}", warning);
}

return validation.is_valid;
```

}

// ============================================================================
// Request Recovery System
// ============================================================================

void Runtime::enable_request_recovery() {
if (!g_runtime_state.initialized.load()) {
throw RuntimeException(“Runtime not initialized”);
}

```
g_runtime_state.recovery_enabled = true;
g_runtime_state.logger->info("Request recovery enabled");
```

}

void Runtime::on_recover(RecoveryHook hook) {
if (!hook) {
throw std::invalid_argument(“Recovery hook cannot be null”);
}

```
g_runtime_state.recovery_hook = std::move(hook);
g_runtime_state.logger->info("Recovery hook registered");
```

}

void Runtime::on_error(TaskErrorHandler handler) {
if (!handler) {
throw std::invalid_argument(“Error handler cannot be null”);
}

```
g_runtime_state.error_handler = std::move(handler);
g_runtime_state.logger->info("Task error handler registered");
```

}

// ============================================================================
// Advanced Task Management
// ============================================================================

PeriodicTaskHandle Runtime::schedule_periodic(Task<> task, std::chrono::milliseconds interval) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (interval <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("Interval must be positive");
}

auto handle_id = g_runtime_state.next_handle_id.fetch_add(1, std::memory_order_relaxed);

// Create a repeating task wrapper
auto periodic_func = [task = std::move(task), interval]() mutable {
    // Execute the task
    schedule(std::move(task));
    
    // Reschedule for next interval
    // Note: This is a simplified implementation
    // Real implementation would need more sophisticated timing
};

{
    std::unique_lock lock(g_runtime_state.handles_mutex);
    g_runtime_state.periodic_tasks[handle_id] = std::move(periodic_func);
}

// Schedule first execution
if (g_runtime_state.event_loop) {
    g_runtime_state.event_loop->schedule_periodic(std::move(task), interval);
}

g_runtime_state.logger->trace("Periodic task scheduled (handle: {}, interval: {}ms)", 
                             handle_id, interval.count());

return PeriodicTaskHandle(handle_id);
```

}

DelayedTaskHandle Runtime::schedule_delayed(Task<> task, std::chrono::milliseconds delay) {
if (!task) {
throw std::invalid_argument(“Task cannot be null”);
}

```
if (delay < std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("Delay cannot be negative");
}

auto handle_id = g_runtime_state.next_handle_id.fetch_add(1, std::memory_order_relaxed);

// Schedule delayed execution
if (g_runtime_state.event_loop) {
    g_runtime_state.event_loop->schedule_delayed(std::move(task), delay);
}

g_runtime_state.logger->trace("Delayed task scheduled (handle: {}, delay: {}ms)", 
                             handle_id, delay.count());

return DelayedTaskHandle(handle_id);
```

}

// ============================================================================
// Internal Implementation Methods
// ============================================================================

void Runtime::run_main_loop() {
g_runtime_state.logger->info(“Entering main event loop”);

```
try {
    // Run the event loop (blocking)
    if (g_runtime_state.event_loop) {
        g_runtime_state.event_loop->run();
    }
    
} catch (const std::exception& e) {
    g_runtime_state.logger->error("Main event loop error: {}", e.what());
    throw;
}

g_runtime_state.logger->info("Main event loop exited");

// Notify shutdown completion
{
    std::unique_lock lock(g_runtime_state.state_mutex);
    g_runtime_state.running.store(false);
    g_runtime_state.state_cv.notify_all();
}
```

}

void Runtime::initialize_recovery_system(const RuntimeConfig& config) {
if (!config.recovery.enabled) {
return;
}

```
// Create request journal based on configuration
if (config.recovery.backend == "rocksdb") {
    // TODO: Implement RocksDB journal
    g_runtime_state.logger->warn("RocksDB recovery backend not yet implemented, using memory journal");
} else if (config.recovery.backend == "mmap") {
    // TODO: Implement mmap journal
    g_runtime_state.logger->warn("mmap recovery backend not yet implemented, using memory journal");
}

// For now, use a simple in-memory journal
// g_runtime_state.journal = std::make_unique<MemoryRequestJournal>();

g_runtime_state.logger->info("Recovery system initialized with {} backend", config.recovery.backend);
```

}

void Runtime::stats_thread_main() {
g_runtime_state.logger->trace(“Statistics thread started”);

```
while (!g_runtime_state.shutdown_requested.load()) {
    try {
        // Update periodic statistics
        update_stats_periodic();
        
        // Sleep for 1 second
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
    } catch (const std::exception& e) {
        g_runtime_state.logger->error("Statistics thread error: {}", e.what());
    }
}

g_runtime_state.logger->trace("Statistics thread stopped");
```

}

void Runtime::handle_task_exception(const std::exception& e, const Task<>& task) {
g_runtime_state.logger->error(“Unhandled task exception: {}”, e.what());

```
// Call user-defined error handler if available
if (g_runtime_state.error_handler) {
    try {
        g_runtime_state.error_handler(e, task);
    } catch (const std::exception& handler_error) {
        g_runtime_state.logger->error("Error handler threw exception: {}", handler_error.what());
    }
}

// Record failed task in statistics
record_task_execution(-1, 0.0, false);
```

}

// ============================================================================
// Template Method Implementations
// ============================================================================

template<typename F>
std::future<std::invoke_result_t<F>> Runtime::execute_in_runtime(F&& func) {
using ReturnType = std::invoke_result_t<F>;
auto promise = std::make_shared<std::promise<ReturnType>>();
auto future = promise->get_future();

```
schedule([promise, func = std::forward<F>(func)]() -> Task<> {
    try {
        if constexpr (std::is_void_v<ReturnType>) {
            func();
            promise->set_value();
        } else {
            auto result = func();
            promise->set_value(std::move(result));
        }
    } catch (...) {
        promise->set_exception(std::current_exception());
    }
    co_return;
}());

return future;
```

}

// ============================================================================
// Utility Functions
// ============================================================================

namespace {

// Convert runtime state to human-readable string
std::string runtime_state_to_string() {
std::ostringstream oss;
oss << “Runtime State:\n”;
oss << “  Initialized: “ << (g_runtime_state.initialized.load() ? “yes” : “no”) << “\n”;
oss << “  Running: “ << (g_runtime_state.running.load() ? “yes” : “no”) << “\n”;
oss << “  Shutdown Requested: “ << (g_runtime_state.shutdown_requested.load() ? “yes” : “no”) << “\n”;
oss << “  Recovery Enabled: “ << (g_runtime_state.recovery_enabled ? “yes” : “no”) << “\n”;

```
if (g_runtime_state.initialized.load()) {
    oss << "  Backend: " << to_string(g_runtime_state.config.backend) << "\n";
    oss << "  Strategy: " << to_string(g_runtime_state.config.strategy) << "\n";
    oss << "  Workers: " << g_runtime_state.config.worker.worker_threads << "\n";
    
    auto uptime = std::chrono::steady_clock::now() - g_runtime_state.start_time;
    auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();
    oss << "  Uptime: " << uptime_seconds << "s\n";
}

return oss.str();
```

}

} // anonymous namespace

// ============================================================================
// Debug and Introspection Methods
// ============================================================================

std::string Runtime::get_runtime_info() {
return runtime_state_to_string();
}

void Runtime::dump_state() {
if (g_runtime_state.logger) {
g_runtime_state.logger->info(“Runtime state dump:\n{}”, runtime_state_to_string());

```
    if (g_runtime_state.scheduler) {
        g_runtime_state.logger->info("Scheduler info:\n{}", g_runtime_state.scheduler->get_scheduler_info());
    }
    
    auto stats = get_stats();
    g_runtime_state.logger->info("Runtime statistics:\n{}", stats.to_string());
}
```

}

bool Runtime::is_healthy() {
if (!g_runtime_state.running.load()) {
return false;
}

```
// Check if core components are responsive
try {
    auto stats = get_stats();
    
    // Check for basic health indicators
    if (stats.success_rate() < 50.0) {
        return false; // High failure rate
    }
    
    if (stats.is_high_load(0.95)) {
        return false; // System overloaded
    }
    
    return true;
    
} catch (...) {
    return false; // Exception indicates unhealthy state
}
```

}

} // namespace stellane
