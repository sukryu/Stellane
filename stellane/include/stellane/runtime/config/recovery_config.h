#pragma once

#include “stellane/runtime/config/config_enums.h”
#include <string>
#include <chrono>
#include <functional>
#include <vector>
#include <unordered_set>
#include <optional>
#include <filesystem>

namespace stellane {

// Forward declarations
class Context;
class Request;
class Response;
struct RecoveryMetadata;

// ============================================================================
// Recovery Backend Configuration
// ============================================================================

/**

- @brief Memory-mapped file journal configuration
- 
- Uses memory-mapped files for fast, persistent request journaling.
- Good balance between performance and durability.
  */
  struct MmapJournalConfig {
  /**
  - @brief Journal file settings
    */
    struct FileSettings {
    size_t initial_size_mb = 64;           ///< Initial journal file size (MB)
    size_t max_size_mb = 1024;             ///< Maximum journal file size (MB)
    bool auto_extend = true;               ///< Automatically extend file when full
    size_t extend_increment_mb = 64;       ///< Size increment when extending (MB)
    
    // File naming and rotation
    std::string filename_pattern = “stellane_journal_{date}_{seq}.log”;
    size_t max_files = 10;                 ///< Maximum number of journal files
    bool compress_old_files = true;        ///< Compress rotated files
    } file_settings;
  
  /**
  - @brief Memory mapping options
    */
    struct MappingOptions {
    bool use_huge_pages = false;           ///< Use huge pages if available
    bool lock_in_memory = false;           ///< Lock pages in memory (mlock)
    bool populate_mapping = true;          ///< Populate mapping at creation
    
    enum class AdviceMode {
    NORMAL = 0,         ///< Normal access pattern
    SEQUENTIAL = 1,     ///< Sequential access pattern
    RANDOM = 2,         ///< Random access pattern
    WILLNEED = 3,       ///< Will need data soon
    DONTNEED = 4        ///< Don’t need data anymore
    };
    
    AdviceMode advice = AdviceMode::SEQUENTIAL;
    } mapping;
  
  /**
  - @brief Synchronization settings
    */
    struct SyncSettings {
    bool sync_on_write = false;            ///< Sync to disk on every write
    std::chrono::milliseconds sync_interval{1000}; ///< Periodic sync interval
    bool use_async_sync = true;            ///< Use asynchronous sync
    
    enum class SyncMode {
    MSYNC = 0,          ///< Use msync() system call
    FSYNC = 1,          ///< Use fsync() on file descriptor
    FDATASYNC = 2       ///< Use fdatasync() for data only
    };
    
    SyncMode mode = SyncMode::FDATASYNC;
    } sync;
  
  /**
  - @brief Validate mmap journal configuration
    */
    [[nodiscard]] bool is_valid() const {
    return file_settings.initial_size_mb > 0 &&
    file_settings.max_size_mb >= file_settings.initial_size_mb &&
    file_settings.extend_increment_mb > 0 &&
    file_settings.max_files > 0 &&
    sync.sync_interval.count() > 0;
    }
    };

/**

- @brief RocksDB journal configuration
- 
- Uses RocksDB for high-performance, transactional request journaling.
- Best choice for high-throughput applications with durability requirements.
  */
  struct RocksDbJournalConfig {
  /**
  - @brief Database options
    */
    struct DatabaseOptions {
    size_t write_buffer_size_mb = 64;      ///< Write buffer size (MB)
    int max_write_buffer_number = 3;       ///< Maximum write buffers
    int min_write_buffer_number_to_merge = 1; ///< Min buffers to merge
    
    // Compaction settings
    int level0_file_num_compaction_trigger = 4; ///< L0 files to trigger compaction
    int level0_slowdown_writes_trigger = 20;    ///< L0 files to slowdown writes
    int level0_stop_writes_trigger = 36;        ///< L0 files to stop writes
    
    // Block cache
    size_t block_cache_size_mb = 256;      ///< Block cache size (MB)
    bool cache_index_and_filter_blocks = true; ///< Cache index/filter blocks
    
    // Compression
    enum class CompressionType {
    NONE = 0,
    SNAPPY = 1,
    ZLIB = 2,
    BZIP2 = 3,
    LZ4 = 4,
    LZ4HC = 5,
    XPRESS = 6,
    ZSTD = 7
    };
    
    CompressionType compression = CompressionType::LZ4;
    } database;
  
  /**
  - @brief Write options
    */
    struct WriteOptions {
    bool sync = false;                      ///< Sync writes to disk
    bool disable_wal = false;               ///< Disable write-ahead log
    bool ignore_missing_column_families = false; ///< Ignore missing CF
    bool no_slowdown = false;               ///< Don’t slowdown on high write rate
    bool low_pri = false;                   ///< Low priority writes
    
    // Batch writing
    bool use_batch_writes = true;           ///< Use batch writes for efficiency
    size_t batch_size = 1000;              ///< Number of entries per batch
    std::chrono::milliseconds batch_timeout{10}; ///< Max time to wait for batch
    } write_options;
  
  /**
  - @brief Read options
    */
    struct ReadOptions {
    bool verify_checksums = true;          ///< Verify checksums on read
    bool fill_cache = true;                ///< Fill block cache on read
    bool tailing = false;                  ///< Enable tailing iterator
    
    // Snapshot settings
    bool use_snapshots = true;             ///< Use snapshots for consistency
    std::chrono::minutes snapshot_ttl{60}; ///< Snapshot time-to-live
    } read_options;
  
  /**
  - @brief Backup and recovery
    */
    struct BackupSettings {
    bool enable_backups = true;            ///< Enable periodic backups
    std::chrono::hours backup_interval{24}; ///< Backup interval
    std::string backup_path = “./backups”; ///< Backup directory
    size_t max_backups = 7;                ///< Maximum backup files to keep
    bool verify_backups = true;            ///< Verify backup integrity
    } backup;
  
  /**
  - @brief Validate RocksDB configuration
    */
    [[nodiscard]] bool is_valid() const {
    return database.write_buffer_size_mb > 0 &&
    database.max_write_buffer_number > 0 &&
    database.min_write_buffer_number_to_merge > 0 &&
    database.block_cache_size_mb > 0 &&
    write_options.batch_size > 0 &&
    write_options.batch_timeout.count() > 0 &&
    backup.backup_interval.count() > 0 &&
    backup.max_backups > 0;
    }
    };

/**

- @brief SQLite journal configuration
- 
- Uses SQLite for simple, embedded request journaling.
- Good for development and small-scale deployments.
  */
  struct SqliteJournalConfig {
  /**
  - @brief Database connection settings
    */
    struct ConnectionSettings {
    std::string database_path = “./stellane_recovery.db”;
    bool create_if_missing = true;         ///< Create database if not exists
    
    // Connection pooling
    size_t max_connections = 10;           ///< Maximum connection pool size
    std::chrono::seconds connection_timeout{30}; ///< Connection timeout
    bool enable_wal_mode = true;           ///< Enable WAL mode for performance
    
    // PRAGMA settings
    size_t cache_size_kb = 10240;          ///< Cache size (10MB)
    std::chrono::seconds busy_timeout{5};  ///< Busy timeout
    bool enable_foreign_keys = true;       ///< Enable foreign key constraints
    } connection;
  
  /**
  - @brief Transaction settings
    */
    struct TransactionSettings {
    enum class IsolationLevel {
    READ_UNCOMMITTED = 0,
    READ_COMMITTED = 1,
    REPEATABLE_READ = 2,
    SERIALIZABLE = 3
    };
    
    IsolationLevel isolation = IsolationLevel::READ_COMMITTED;
    bool auto_commit = false;               ///< Auto-commit transactions
    std::chrono::milliseconds transaction_timeout{1000}; ///< Transaction timeout
    
    // Batch processing
    bool use_batch_transactions = true;    ///< Use batch transactions
    size_t batch_size = 100;               ///< Entries per transaction
    std::chrono::milliseconds batch_timeout{100}; ///< Batch timeout
    } transactions;
  
  /**
  - @brief Schema and table settings
    */
    struct SchemaSettings {
    std::string table_name = “request_journal”;
    bool auto_vacuum = true;                ///< Enable auto-vacuum
    std::chrono::hours vacuum_interval{24}; ///< Vacuum interval
    
    // Indexing
    std::vector<std::string> custom_indices = {
    “CREATE INDEX IF NOT EXISTS idx_timestamp ON request_journal(timestamp)”,
    “CREATE INDEX IF NOT EXISTS idx_status ON request_journal(status)”
    };
    } schema;
  
  /**
  - @brief Validate SQLite configuration
    */
    [[nodiscard]] bool is_valid() const {
    return !connection.database_path.empty() &&
    connection.max_connections > 0 &&
    connection.connection_timeout.count() > 0 &&
    connection.cache_size_kb > 0 &&
    transactions.transaction_timeout.count() > 0 &&
    transactions.batch_size > 0 &&
    transactions.batch_timeout.count() > 0 &&
    schema.vacuum_interval.count() > 0;
    }
    };

// ============================================================================
// Recovery Policy Configuration
// ============================================================================

/**

- @brief Request filtering and selection policies
- 
- Controls which requests are journaled and recovered.
  */
  struct RecoveryPolicyConfig {
  /**
  - @brief Request filtering criteria
    */
    struct FilterCriteria {
    // HTTP method filtering
    std::unordered_set<std::string> included_methods = {“POST”, “PUT”, “PATCH”, “DELETE”};
    std::unordered_set<std::string> excluded_methods = {“GET”, “HEAD”, “OPTIONS”};
    
    // Path-based filtering
    std::vector<std::string> included_path_patterns;    ///< Include paths matching patterns
    std::vector<std::string> excluded_path_patterns = { ///< Exclude paths matching patterns
    “/health”, “/metrics”, “/static/*”, “*.css”, “*.js”, “*.png”, “*.jpg”
    };
    
    // Content-Type filtering
    std::unordered_set<std::string> included_content_types;
    std::unordered_set<std::string> excluded_content_types = {
    “text/css”, “text/javascript”, “image/*”, “application/javascript”
    };
    
    // Request size limits
    std::optional<size_t> min_body_size;   ///< Minimum body size to journal
    std::optional<size_t> max_body_size;   ///< Maximum body size to journal
    
    // Custom filtering function
    std::function<bool(const Request&)> custom_filter;
    } filters;
  
  /**
  - @brief Idempotency detection
    */
    struct IdempotencySettings {
    bool enable_idempotency_detection = true; ///< Enable idempotency checks
    
    // Idempotency key sources
    std::vector<std::string> idempotency_headers = {
    “Idempotency-Key”, “X-Idempotency-Key”, “Request-Id”
    };
    
    // Automatic idempotency detection
    bool detect_from_content = true;       ///< Detect from request content
    bool detect_from_parameters = true;    ///< Detect from query parameters
    
    // Idempotency cache
    std::chrono::hours idempotency_window{24}; ///< Window for idempotency checking
    size_t max_idempotency_entries = 100000;   ///< Max entries in cache
    } idempotency;
  
  /**
  - @brief Recovery prioritization
    */
    struct PrioritySettings {
    /**
    - @brief Recovery priority levels
      */
      enum class Priority {
      LOW = 0,        ///< Low priority (background recovery)
      NORMAL = 1,     ///< Normal priority
      HIGH = 2,       ///< High priority (immediate recovery)
      CRITICAL = 3    ///< Critical priority (block until recovered)
      };
    
    // Priority assignment rules
    std::unordered_map<std::string, Priority> method_priorities = {
    {“POST”, Priority::HIGH},
    {“PUT”, Priority::HIGH},
    {“PATCH”, Priority::NORMAL},
    {“DELETE”, Priority::CRITICAL}
    };
    
    std::vector<std::pair<std::string, Priority>> path_priorities; ///< Path pattern -> priority
    
    // Custom priority function
    std::function<Priority(const Request&)> custom_priority_function;
    } priority;
  
  /**
  - @brief Recovery timing and limits
    */
    struct TimingSettings {
    std::chrono::seconds max_recovery_time{300};   ///< Maximum time for recovery
    std::chrono::seconds recovery_delay{1};        ///< Delay before starting recovery
    std::chrono::hours max_recovery_age{72};       ///< Maximum age of recoverable requests
    
    // Retry settings
    size_t max_retry_attempts = 3;         ///< Maximum recovery attempts
    std::chrono::seconds retry_backoff{5}; ///< Backoff between retries
    double backoff_multiplier = 2.0;       ///< Exponential backoff multiplier
    std::chrono::seconds max_retry_delay{300}; ///< Maximum retry delay
    } timing;
  
  /**
  - @brief Check if request should be journaled
  - @param request Request to check
  - @return true if request should be journaled
    */
    [[nodiscard]] bool should_journal_request(const Request& request) const {
    // Check HTTP method
    if (!filters.included_methods.empty() &&
    filters.included_methods.find(request.method()) == filters.included_methods.end()) {
    return false;
    }
    
    if (filters.excluded_methods.find(request.method()) != filters.excluded_methods.end()) {
    return false;
    }
    
    // Check content type
    auto content_type = request.header(“Content-Type”);
    if (content_type && !filters.excluded_content_types.empty()) {
    for (const auto& excluded : filters.excluded_content_types) {
    if (content_type->find(excluded) != std::string::npos) {
    return false;
    }
    }
    }
    
    // Check body size
    auto body_size = request.body().size();
    if (filters.min_body_size && body_size < *filters.min_body_size) {
    return false;
    }
    if (filters.max_body_size && body_size > *filters.max_body_size) {
    return false;
    }
    
    // Apply custom filter
    if (filters.custom_filter && !filters.custom_filter(request)) {
    return false;
    }
    
    return true;
    }
  
  /**
  - @brief Get recovery priority for request
  - @param request Request to prioritize
  - @return Recovery priority level
    */
    [[nodiscard]] PrioritySettings::Priority get_recovery_priority(const Request& request) const {
    // Custom priority function takes precedence
    if (priority.custom_priority_function) {
    return priority.custom_priority_function(request);
    }
    
    // Check method priorities
    auto method_it = priority.method_priorities.find(request.method());
    if (method_it != priority.method_priorities.end()) {
    return method_it->second;
    }
    
    // Check path priorities
    for (const auto& [pattern, prio] : priority.path_priorities) {
    if (request.path().find(pattern) != std::string::npos) {
    return prio;
    }
    }
    
    return PrioritySettings::Priority::NORMAL;
    }
  
  /**
  - @brief Validate recovery policy configuration
    */
    [[nodiscard]] bool is_valid() const {
    return timing.max_recovery_time.count() > 0 &&
    timing.recovery_delay.count() >= 0 &&
    timing.max_recovery_age.count() > 0 &&
    timing.max_retry_attempts > 0 &&
    timing.retry_backoff.count() > 0 &&
    timing.backoff_multiplier > 0.0 &&
    timing.max_retry_delay.count() > 0 &&
    idempotency.idempotency_window.count() > 0 &&
    idempotency.max_idempotency_entries > 0;
    }
    };

// ============================================================================
// Recovery Hook Configuration
// ============================================================================

/**

- @brief Recovery hook and callback configuration
- 
- Defines custom recovery logic and error handling.
  */
  struct RecoveryHookConfig {
  /**
  - @brief Recovery hook function types
    */
    using BasicRecoveryHook = std::function<Task<>(Context&, const Request&)>;
    using AdvancedRecoveryHook = std::function<Task<>(Context&, const Request&, const RecoveryMetadata&)>;
    using ErrorHandler = std::function<void(const std::exception&, const Request&, const RecoveryMetadata&)>;
  
  /**
  - @brief Hook registration
    */
    struct HookRegistration {
    BasicRecoveryHook basic_hook;           ///< Basic recovery hook
    AdvancedRecoveryHook advanced_hook;     ///< Advanced recovery hook with metadata
    ErrorHandler error_handler;            ///< Error handling callback
    
    // Hook selection
    bool prefer_advanced_hook = true;      ///< Prefer advanced hook if available
    bool fallback_to_basic = true;         ///< Fallback to basic hook if advanced fails
    } hooks;
  
  /**
  - @brief Recovery context configuration
    */
    struct ContextConfig {
    bool preserve_original_context = true;  ///< Preserve original request context
    bool create_recovery_context = true;    ///< Create new recovery context
    
    // Context data to preserve
    std::vector<std::string> preserved_headers = {
    “Authorization”, “X-User-ID”, “X-Session-ID”, “X-Trace-ID”
    };
    
    // Recovery-specific context
    std::unordered_map<std::string, std::string> recovery_headers = {
    {“X-Recovery-Attempt”, “true”},
    {“X-Recovery-Source”, “stellane-runtime”}
    };
    
    // Timeout settings
    std::chrono::seconds hook_timeout{30};  ///< Timeout for hook execution
    bool cancel_on_timeout = true;          ///< Cancel recovery on timeout
    } context;
  
  /**
  - @brief Recovery result handling
    */
    struct ResultHandling {
    /**
    - @brief Recovery outcome types
      */
      enum class Outcome {
      SUCCESS = 0,        ///< Recovery succeeded
      PARTIAL_SUCCESS = 1,///< Partial recovery (some data lost)
      FAILED = 2,         ///< Recovery failed
      SKIPPED = 3,        ///< Recovery skipped (e.g., not needed)
      DEFERRED = 4        ///< Recovery deferred to later
      };
    
    // Outcome handling callbacks
    std::function<void(const Request&, Outcome)> outcome_callback;
    
    // Metrics and logging
    bool log_recovery_attempts = true;      ///< Log all recovery attempts
    bool log_recovery_outcomes = true;      ///< Log recovery outcomes
    bool emit_recovery_metrics = true;      ///< Emit recovery metrics
    
    // Notification settings
    bool notify_on_failure = true;          ///< Notify on recovery failure
    bool notify_on_success = false;         ///< Notify on recovery success
    std::vector<std::string> notification_endpoints; ///< Webhook endpoints for notifications
    } result_handling;
  
  /**
  - @brief Validate recovery hook configuration
    */
    [[nodiscard]] bool is_valid() const {
    // At least one hook must be configured
    bool has_hook = static_cast<bool>(hooks.basic_hook) ||
    static_cast<bool>(hooks.advanced_hook);
    
    return has_hook &&
    context.hook_timeout.count() > 0;
    }
    };

// ============================================================================
// Main Recovery Configuration
// ============================================================================

/**

- @brief Comprehensive recovery system configuration
- 
- Combines all recovery-related settings including backend selection,
- policy configuration, and hook management.
  */
  struct RecoveryConfig {
  // ========================================================================
  // Basic Recovery Settings
  // ========================================================================
  
  /**
  - @brief Enable recovery system
    */
    bool enabled = false;
  
  /**
  - @brief Recovery backend selection
    */
    RecoveryBackend backend = RecoveryBackend::MMAP;
  
  /**
  - @brief Recovery journal file path
    */
    std::string path = “./stellane_recovery”;
  
  /**
  - @brief Maximum recovery attempts per request
    */
    int max_attempts = 3;
  
  /**
  - @brief Recovery operation timeout
    */
    std::chrono::seconds timeout{30};
  
  // ========================================================================
  // Backend-Specific Configurations
  // ========================================================================
  
  MmapJournalConfig mmap;                    ///< Memory-mapped file journal config
  RocksDbJournalConfig rocksdb;              ///< RocksDB journal config
  SqliteJournalConfig sqlite;                ///< SQLite journal config
  
  // ========================================================================
  // Recovery Policy and Behavior
  // ========================================================================
  
  RecoveryPolicyConfig policy;               ///< Recovery policy and filtering
  RecoveryHookConfig hooks;                  ///< Recovery hooks and callbacks
  
  // ========================================================================
  // Journal Management
  // ========================================================================
  
  /**
  - @brief Journal rotation settings
    */
    struct JournalRotation {
    bool enable_rotation = true;           ///< Enable journal rotation
    size_t max_file_size_mb = 100;         ///< Maximum file size before rotation
    size_t max_files = 10;                 ///< Maximum number of journal files
    bool compress_old_files = true;        ///< Compress rotated files
    std::chrono::hours cleanup_interval{24}; ///< Cleanup interval for old files
    std::chrono::days max_file_age{7};     ///< Maximum age of journal files
    } journal;
  
  // ========================================================================
  // Performance and Monitoring
  // ========================================================================
  
  /**
  - @brief Recovery performance settings
    */
    struct Performance {
    size_t journal_buffer_size = 1048576;  ///< Journal buffer size (1MB)
    bool async_journaling = true;          ///< Asynchronous journaling
    size_t recovery_thread_pool_size = 2;  ///< Recovery worker threads
    
    // Rate limiting
    size_t max_recoveries_per_second = 100; ///< Rate limit for recovery operations
    bool enable_backpressure = true;       ///< Enable backpressure on high load
    
    // Monitoring
    bool enable_metrics = true;            ///< Enable recovery metrics
    std::chrono::seconds metrics_interval{60}; ///< Metrics collection interval
    } performance;
  
  // ========================================================================
  // Utility Methods
  // ========================================================================
  
  /**
  - @brief Get backend-specific configuration
  - @return Configuration for the selected backend
    */
    [[nodiscard]] std::string get_backend_info() const {
    switch (backend) {
    case RecoveryBackend::DISABLED: return “Recovery disabled”;
    case RecoveryBackend::MMAP: return “Memory-mapped file journal”;
    case RecoveryBackend::ROCKSDB: return “RocksDB embedded database”;
    case RecoveryBackend::SQLITE: return “SQLite embedded database”;
    case RecoveryBackend::CUSTOM: return “Custom recovery backend”;
    default: return “Unknown backend”;
    }
    }
  
  /**
  - @brief Estimate storage requirements
  - @param requests_per_day Expected requests per day
  - @param avg_request_size Average request size in bytes
  - @return Estimated storage requirements in bytes
    */
    [[nodiscard]] size_t estimate_storage_requirements(size_t requests_per_day,
    size_t avg_request_size) const {
    if (!enabled) return 0;
    
    // Calculate journaled requests (applying filters)
    double filter_ratio = 0.3; // Assume 30% of requests are journaled
    size_t journaled_requests = static_cast<size_t>(requests_per_day * filter_ratio);
    
    // Add metadata overhead (timestamps, status, etc.)
    size_t metadata_overhead = 256; // bytes per request
    size_t total_per_request = avg_request_size + metadata_overhead;
    
    // Calculate daily storage
    size_t daily_storage = journaled_requests * total_per_request;
    
    // Account for retention period
    size_t retention_days = journal.max_file_age.count();
    
    // Add compression savings for older files
    double compression_ratio = journal.compress_old_files ? 0.6 : 1.0;
    
    return static_cast<size_t>(daily_storage * retention_days * compression_ratio);
    }
  
  /**
  - @brief Validate entire recovery configuration
  - @return true if configuration is valid
    */
    [[nodiscard]] bool is_valid() const {
    if (!enabled) return true; // Disabled config is always valid
    
    // Basic validation
    if (path.empty() || max_attempts <= 0 || timeout.count() <= 0) {
    return false;
    }
    
    // Backend-specific validation
    bool backend_valid = false;
    switch (backend) {
    case RecoveryBackend::DISABLED:
    backend_valid = true;
    break;
    case RecoveryBackend::MMAP:
    backend_valid = mmap.is_valid();
    break;
    case RecoveryBackend::ROCKSDB:
    backend_valid = rocksdb.is_valid();
    break;
    case RecoveryBackend::SQLITE:
    backend_valid = sqlite.is_valid();
    break;
    case RecoveryBackend::CUSTOM:
    backend_valid = true; // Custom validation not implemented
    break;
    }
    
    if (!backend_valid) return false;
    
    // Validate sub-configurations
    return policy.is_valid() &&
    hooks.is_valid() &&
    journal.max_file_size_mb > 0 &&
    journal.max_files > 0 &&
    journal.cleanup_interval.count() > 0 &&
    journal.max_file_age.count() > 0 &&
    performance.journal_buffer_size > 0 &&
    performance.recovery_thread_pool_size > 0 &&
    performance.max_recoveries_per_second > 0 &&
    performance.metrics_interval.count() > 0;
    }
  
  /**
  - @brief Create default configuration for development
  - @return Development-optimized recovery configuration
    */
    static RecoveryConfig create_development_config() {
    RecoveryConfig config;
    config.enabled = true;
    config.backend = RecoveryBackend::SQLITE;
    config.path = “./dev_recovery.db”;
    config.max_attempts = 2;
    config.timeout = std::chrono::seconds(10);
    
    // Relaxed policies for development
    config.policy.filters.included_methods = {“POST”, “PUT”, “DELETE”};
    config.policy.timing.max_recovery_time = std::chrono::seconds(60);
    
    // Reduced performance requirements
    config.performance.recovery_thread_pool_size = 1;
    config.performance.max_recoveries_per_second = 10;
    
    // Smaller journal files
    config.journal.max_file_size_mb = 10;
    config.journal.max_files = 3;
    
    return config;
    }
  
  /**
  - @brief Create default configuration for production
  - @return Production-optimized recovery configuration
    */
    static RecoveryConfig create_production_config() {
    RecoveryConfig config;
    config.enabled = true;
    config.backend = RecoveryBackend::ROCKSDB;
    config.path = “/var/log/stellane/recovery”;
    config.max_attempts = 5;
    config.timeout = std::chrono::seconds(30);
    
    // Comprehensive policies for production
    config.policy.filters.included_methods = {“POST”, “PUT”, “PATCH”, “DELETE”};
    config.policy.timing.max_recovery_time = std::chrono::seconds(300);
    config.policy.timing.max_recovery_age = std::chrono::hours(168); // 7 days
    
    // High performance settings
    config.performance.recovery_thread_pool_size = 4;
    config.performance.max_recoveries_per_second = 1000;
    config.performance.async_journaling = true;
    
    // Robust journal management
    config.journal.max_file_size_mb = 1024; // 1GB
    config.journal.max_files = 30;
    config.journal.compress_old_files = true;
    
    return config;
    }
    };

} // namespace stellane
