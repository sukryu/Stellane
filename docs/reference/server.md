# Server API Reference

The Server class is the core entry point for Stellane applications, providing HTTP server initialization, router mounting, middleware registration, and request lifecycle management.

## Table of Contents

- [Overview](#overview)
- [Class Definition](#class-definition)
- [Constructor](#constructor)
- [Methods](#methods)
  - [mount()](#mount)
  - [use()](#use)
  - [listen()](#listen)
  - [enable_request_recovery()](#enable_request_recovery)
  - [on_recover()](#on_recover)
- [Usage Examples](#usage-examples)
- [Integration Points](#integration-points)
- [Configuration](#configuration)
- [Error Handling](#error-handling)
- [Performance Considerations](#performance-considerations)
- [Testing](#testing)
- [Future Roadmap](#future-roadmap)

## Overview

The `Server` class orchestrates the following core responsibilities:

- **Router Management**: Mount Router instances under specific URI prefixes
- **Middleware Pipeline**: Register global middleware for request/response processing
- **Server Lifecycle**: Start and manage the HTTP server event loop
- **Request Recovery**: Handle failed requests with custom recovery logic
- **Runtime Integration**: Interface with the async task scheduler and event loop

```cpp
#include "stellane/server.h"

class Server {
public:
    Server();
    ~Server();

    // Router mounting
    void mount(const std::string& prefix, const Router& router);
    void mount(const std::string& prefix, std::shared_ptr<Router> router);

    // Middleware registration
    void use(Middleware middleware);
    void use(std::shared_ptr<Middleware> middleware);

    // Server lifecycle
    void listen(uint16_t port);
    void listen(const std::string& host, uint16_t port);
    void stop();

    // Request recovery
    void enable_request_recovery();
    void on_recover(std::function<Task<>(Context&, const Request&)> handler);

    // Configuration
    void set_max_connections(size_t count);
    void set_request_timeout(std::chrono::milliseconds timeout);
    void set_keep_alive_timeout(std::chrono::milliseconds timeout);

    // Introspection
    std::vector<std::string> mounted_routes() const;
    size_t middleware_count() const;
    bool is_running() const;
};
```

## Constructor

### Server()

Creates a new Server instance with default configuration.

```cpp
Server server;
```

**Default Configuration:**

- Max connections: 1000
- Request timeout: 30 seconds
- Keep-alive timeout: 5 seconds
- Request recovery: disabled

## Methods

### mount()

Registers a Router instance under a specific URI prefix.

```cpp
void mount(const std::string& prefix, const Router& router);
void mount(const std::string& prefix, std::shared_ptr<Router> router);
```

**Parameters:**

- `prefix`: URI path prefix (e.g., “/api/v1”, “/users”)
- `router`: Router instance containing route definitions

**Example:**

```cpp
Router user_router;
user_router.get("/:id", get_user_handler);
user_router.post("/", create_user_handler);

Server server;
server.mount("/users", user_router);
// Routes become: GET /users/:id, POST /users/
```

**Route Resolution:**

- Prefix paths are normalized (trailing slashes removed)
- Route patterns are merged into the server’s routing tree
- Conflicts between mounted routes throw `RouteConflictException`

### use()

Registers global middleware to be applied to all requests.

```cpp
void use(Middleware middleware);
void use(std::shared_ptr<Middleware> middleware);
```

**Parameters:**

- `middleware`: Middleware instance implementing the middleware interface

**Example:**

```cpp
server.use(LoggingMiddleware{});
server.use(std::make_shared<AuthMiddleware>());
server.use(CorsMiddleware{});
```

**Middleware Order:**

- Middleware is executed in registration order
- Each middleware can modify the request/response or halt processing
- See [Middleware Concepts](../concepts/middleware.md) for implementation details

### listen()

Starts the HTTP server on the specified port and host.

```cpp
void listen(uint16_t port);
void listen(const std::string& host, uint16_t port);
```

**Parameters:**

- `port`: TCP port number (1-65535)
- `host`: Host address (default: “0.0.0.0”)

**Example:**

```cpp
server.listen(8080);                    // Listen on all interfaces
server.listen("127.0.0.1", 3000);      // Listen on localhost only
```

**Behavior:**

- This method blocks until the server is stopped
- Internally calls `Runtime::start()` to begin the event loop
- Throws `ServerStartException` if the port is already in use

### enable_request_recovery()

Activates the request recovery system for handling failed requests.

```cpp
void enable_request_recovery();
```

**Usage:**

```cpp
server.enable_request_recovery();
server.on_recover(recovery_handler);
```

**Recovery Scenarios:**

- Server crashes during request processing
- Network timeouts or connection drops
- Transient resource unavailability

### on_recover()

Registers a custom handler for request recovery scenarios.

```cpp
void on_recover(std::function<Task<>(Context&, const Request&)> handler);
```

**Parameters:**

- `handler`: Async function to handle request recovery

**Example:**

```cpp
server.on_recover([](Context& ctx, const Request& req) -> Task<> {
    if (req.method() == "POST" || req.method() == "PUT") {
        // Check if operation was already completed
        if (co_await is_operation_completed(req)) {
            ctx.response().status(200);
            co_return;
        }
        
        // Retry the operation
        co_await retry_original_handler(ctx, req);
    }
    
    ctx.logger().info("Request {} recovered successfully", req.path());
});
```

## Usage Examples

### Basic Server Setup

```cpp
#include "stellane/server.h"

Task<> hello_handler(Context& ctx) {
    ctx.response().json({{"message", "Hello, World!"}});
    co_return;
}

int main() {
    Server server;
    
    Router api_router;
    api_router.get("/hello", hello_handler);
    
    server.mount("/api", api_router);
    server.listen(8080);
    
    return 0;
}
```

### Full-Featured Application

```cpp
int main() {
    Server server;
    
    // Global middleware
    server.use(LoggingMiddleware{});
    server.use(CorsMiddleware{});
    server.use(AuthMiddleware{});
    
    // Mount routers
    Router user_router;
    user_router.get("/:id", get_user);
    user_router.post("/", create_user);
    user_router.put("/:id", update_user);
    user_router.delete("/:id", delete_user);
    
    Router auth_router;
    auth_router.post("/login", login_handler);
    auth_router.post("/logout", logout_handler);
    
    server.mount("/users", user_router);
    server.mount("/auth", auth_router);
    
    // Configure recovery
    server.enable_request_recovery();
    server.on_recover([](Context& ctx, const Request& req) -> Task<> {
        ctx.logger().warn("Recovering request: {} {}", req.method(), req.path());
        co_await retry_with_backoff(ctx, req);
    });
    
    // Server configuration
    server.set_max_connections(5000);
    server.set_request_timeout(std::chrono::seconds(60));
    
    // Start server
    std::cout << "Server starting on port 8080..." << std::endl;
    server.listen(8080);
    
    return 0;
}
```

### Testing Server

```cpp
#include "stellane/testing.h"

TEST(ServerTest, RouteHandling) {
    Server server;
    
    Router router;
    router.get("/test", [](Context& ctx) -> Task<> {
        ctx.response().text("test response");
        co_return;
    });
    
    server.mount("/", router);
    
    auto response = co_await server.test_request(
        Request::get("/test")
    );
    
    EXPECT_EQ(response.status(), 200);
    EXPECT_EQ(response.body(), "test response");
}
```

## Integration Points

### Runtime Integration

The Server class integrates tightly with Stellane’s async runtime:

```cpp
// Server internally manages:
class Server {
private:
    std::unique_ptr<Runtime> runtime_;
    TaskQueue request_queue_;
    ContextFactory context_factory_;
    RouterTree routing_tree_;
    MiddlewareChain middleware_chain_;
};
```

### Component Dependencies

|Component   |Purpose                      |Documentation                                   |
|------------|-----------------------------|------------------------------------------------|
|`Router`    |Route definition and matching|[Router Reference](./router.md)                 |
|`Middleware`|Request/response processing  |[Middleware Concepts](../concepts/middleware.md)|
|`Runtime`   |Async task scheduling        |[Runtime Reference](./runtime.md)               |
|`Context`   |Request state management     |[Context Reference](./context.md)               |
|`Request`   |HTTP request abstraction     |[Request Reference](./request.md)               |
|`Response`  |HTTP response building       |[Response Reference](./response.md)             |

## Configuration

### Configuration File (stellane.config.toml)

```toml
[server]
port = 8080
host = "0.0.0.0"
max_connections = 1000
request_timeout = "30s"
keep_alive_timeout = "5s"
enable_recovery = true

[server.tls]
enabled = false
cert_file = "cert.pem"
key_file = "key.pem"

[middleware]
order = ["logging", "cors", "auth"]

[logging]
level = "info"
format = "json"
```

### Programmatic Configuration

```cpp
server.set_max_connections(2000);
server.set_request_timeout(std::chrono::seconds(45));
server.set_keep_alive_timeout(std::chrono::seconds(10));
```

## Error Handling

### Common Exceptions

```cpp
try {
    server.listen(8080);
} catch (const ServerStartException& e) {
    std::cerr << "Failed to start server: " << e.what() << std::endl;
} catch (const RouteConflictException& e) {
    std::cerr << "Route conflict: " << e.what() << std::endl;
}
```

### Exception Types

- `ServerStartException`: Port binding failures, permission issues
- `RouteConflictException`: Duplicate route patterns
- `MiddlewareException`: Middleware initialization failures
- `ConfigurationException`: Invalid configuration values

## Performance Considerations

### Scalability Factors

1. **Connection Management**
- Default max connections: 1000
- Each connection consumes ~8KB memory
- Adjust based on available system resources
1. **Request Processing**
- Middleware chain overhead: ~10-50μs per middleware
- Router lookup: O(log n) for n routes
- Context creation: ~1-5μs per request
1. **Memory Usage**
- Base server: ~1MB
- Per connection: ~8KB
- Per request context: ~2KB

### Optimization Tips

```cpp
// Optimize for high-throughput scenarios
server.set_max_connections(10000);
server.set_keep_alive_timeout(std::chrono::seconds(1));

// Minimize middleware chain
server.use(EssentialMiddleware{});  // Only critical middleware

// Use efficient routing patterns
router.get("/api/users/:id", handler);  // Preferred
router.get("/api/users/{id}", handler); // Less efficient
```

## Testing

### Unit Testing

```cpp
TEST(ServerTest, MiddlewareOrdering) {
    Server server;
    
    auto middleware1 = std::make_shared<TestMiddleware>("first");
    auto middleware2 = std::make_shared<TestMiddleware>("second");
    
    server.use(middleware1);
    server.use(middleware2);
    
    EXPECT_EQ(server.middleware_count(), 2);
    
    auto response = co_await server.test_request(
        Request::get("/")
    );
    
    // Verify middleware execution order
    EXPECT_EQ(response.header("X-Middleware-Order"), "first,second");
}
```

### Integration Testing

```cpp
TEST(ServerIntegrationTest, FullRequestCycle) {
    Server server;
    
    // Setup routes and middleware
    setup_test_routes(server);
    
    // Start server in background
    auto server_task = server.start_async(0);  // Random port
    auto port = server.get_port();
    
    // Make real HTTP requests
    auto client = HttpClient{};
    auto response = co_await client.get(f"http://localhost:{port}/api/users/123");
    
    EXPECT_EQ(response.status(), 200);
    
    server.stop();
    co_await server_task;
}
```

## Future Roadmap

### Planned Features

|Feature                 |Description                             |Target Version|
|------------------------|----------------------------------------|--------------|
|**HTTPS Support**       |TLS/SSL encryption with `listen_tls()`  |v1.2          |
|**WebSocket Support**   |Upgrade HTTP connections to WebSocket   |v1.3          |
|**Hot Reload**          |Configuration changes without restart   |v1.4          |
|**Graceful Shutdown**   |SIGINT handling with connection draining|v1.2          |
|**Metrics Integration** |Built-in Prometheus metrics             |v1.3          |
|**Multi-port Listening**|Listen on multiple ports simultaneously |v1.4          |

### API Evolution

```cpp
// Future API additions
class Server {
public:
    // HTTPS support
    void listen_tls(uint16_t port, const TlsConfig& config);
    
    // WebSocket support
    void enable_websocket();
    void on_websocket_upgrade(WebSocketHandler handler);
    
    // Graceful shutdown
    void shutdown(std::chrono::seconds timeout = std::chrono::seconds(30));
    
    // Metrics
    ServerMetrics metrics() const;
    
    // Multi-port support
    void listen_multi(const std::vector<ListenConfig>& configs);
};
```

### Breaking Changes

- **v1.2**: `listen()` will return `Task<>` instead of blocking
- **v1.3**: Default request timeout reduced to 15 seconds
- **v1.4**: Middleware interface refactored for better performance

-----

## See Also

- [Router Reference](./router.md) - Route definition and matching
- [Middleware Concepts](../concepts/middleware.md) - Request/response processing
- [Runtime Reference](./runtime.md) - Async task scheduling
- [Getting Started Guide](../getting-started.md) - Build your first Stellane app
- [Configuration Reference](../reference/configuration.md) - Server configuration options
