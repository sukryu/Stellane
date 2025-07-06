# Stellane Server Specification

The **Stellane Server** is a high-performance networking component of the Stellane Ecosystem, a C++20/23/26 backend framework for game servers and real-time applications. It handles HTTP and WebSocket requests with low latency and high throughput, integrating seamlessly with the [Router](router-spec.md) and [Zenix++ ORM](zenix-orm-spec.md). Built on STL (C++20/23/26 Co-routines, `std::expected`, Concepts) and Unifex, with optional Boost.Asio for large-scale scenarios, the Server ensures **blazing-fast performance**, **exceptional developer experience (DX)**, and **ironclad security**.

This document details the Server's architecture, configuration, and usage, including its modular `NetworkBackend` design, cross-platform support, and CLI integration.

---

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
- [Server Configuration](#server-configuration)
  - [stellane.template.toml](#stellanetemplatetoml)
  - [.env](#env)
- [NetworkBackend Concept](#networkbackend-concept)
  - [STL Backend](#stl-backend)
  - [Unifex Backend](#unifex-backend)
  - [Boost.Asio Backend (Optional)](#boostasio-backend-optional)
- [Usage](#usage)
  - [C++ API](#c-api)
  - [CLI Integration](#cli-integration)
- [Cross-Platform Considerations](#cross-platform-considerations)
- [Security Mechanisms](#security-mechanisms)
- [Performance Characteristics](#performance-characteristics)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [References](#references)

---

## Overview

The Stellane Server is designed for game servers and real-time applications, supporting HTTP (REST) and WebSocket protocols with minimal latency (<10ms) and high throughput (>1,000 req/s for small servers, >10,000 for large). It leverages:

- **C++20/23/26**: Co-routines for async I/O, `std::expected` for type-safe error handling, Concepts for API constraints.
- **Unifex**: Lightweight async programming for mid-scale applications.
- **Boost.Asio** (optional): For massive scale (>10,000 connections).
- **Router Integration**: Dispatches requests to `Router::dispatch` for endpoint handling.
- **CLI Integration**: Executes via `stellane run`, loading `.env` and logging to `~/.stellane/logs`.

**Key Features**:
- **Performance**: Event-driven model (`select`/`epoll`/`kqueue`), Co-routines, and connection pooling.
- **DX**: Simple API (`server.run()`), dynamic configuration via `stellane.template.toml`.
- **Security**: No `sudo`, 0700/0600 permissions, TLS support (OpenSSL or platform-native).
- **Cross-Platform**: Windows (WinSock2), Linux (epoll), macOS (kqueue).

---

## Architecture

The Server follows a modular design, abstracting networking through the `NetworkBackend` concept. It integrates with the `Router` for request dispatching and supports HTTP/WebSocket protocols.

**Architecture Diagram**:
```plaintext
[CLI: stellane run] --> [.env: Load Config] --> [Server: Initialize]
    ↓
[NetworkBackend: Accept Connections]
    ↓
[Request: HTTP/WebSocket] --> [Router: Dispatch] --> [Handler: Process]
    ↓
[Response: Send] --> [Zenix++ ORM: DB Access (Optional)]
```
Components:
	•	Server: Main entry point, initializes NetworkBackend and Router.
	•	NetworkBackend: Abstract interface for networking (STL, Unifex, or Boost.Asio).
	•	Router: Dispatches requests to handlers (see Router Specification).
	•	Zenix++ ORM: Database access (optional, see Zenix++ ORM Specification).

## Server Configuration
### stellane.template.toml
The stellane.template.toml file defines server configuration, including port, TLS, and backend selection.
Schema (Server Section):
```
[server]
port =               # Listening port (e.g., 8080)
tls_enabled =        # Enable TLS (default: false)
backend = "" # Networking backend (default: stl)
Example:
[server]
port = 8080
tls_enabled = false
backend = "stl"
```
.env
Environment variables override stellane.template.toml settings and are loaded by stellane run.
Example:
```
PORT=8080
TLS_ENABLED=false
NETWORK_BACKEND=stl
DATABASE_URL=postgres://user:pass@localhost:5432/db
```
## NetworkBackend Concept
The NetworkBackend concept defines the interface for networking implementations, allowing seamless switching between STL, Unifex, and Boost.Asio.
Definition:
```cpp
template
concept NetworkBackend = requires(T t, int port, Request& req, Response& res) {
    { t.accept_connections(port) } -> std::same_as>;
    { t.handle_request(req, res) } -> std::same_as>;
};
Interface:
class NetworkBackend {
public:
    virtual ~NetworkBackend() = default;
    virtual std::future accept_connections(int port) = 0;
    virtual std::future handle_request(Request& req, Response& res) = 0;
};
```
## STL Backend
  •	Implementation: Uses select (simple) or epoll/kqueue (scalable) with C++20 Co-routines.
	•	Use Case: Small to medium-scale servers (<1,000 connections).
	•	Advantages: No dependencies, lightweight (~1MB binary).
	•	Limitations: Manual platform-specific socket handling.
Example:
```cpp
class STLNetworkBackend : public NetworkBackend {
public:
    std::future accept_connections(int port) override {
        co_await async_listen(port);
        while (true) {
            auto socket = co_await async_accept();
            // Handle connection
        }
    }
    std::future handle_request(Request& req, Response& res) override {
        // Parse request, dispatch to Router
        co_return;
    }
};
```
## Unifex Backend
  •	Implementation: Uses unifex::io_context and unifex::task for async I/O.
	•	Use Case: Medium-scale servers (~5,000 connections).
	•	Advantages: Lightweight (~2-3MB), C++20 Co-routine integration.
	•	Limitations: Experimental, smaller community.
Example:
```cpp
class UnifexNetworkBackend : public NetworkBackend {
public:
    std::future accept_connections(int port) override {
        unifex::io_context ctx;
        co_await unifex::async_listen(ctx, port);
        // Handle connections
        co_return;
    }
};
```
## Boost.Asio Backend (Optional). 
  •	Implementation: Uses Boost.Asio’s Proactor pattern (asio::io_context).
	•	Use Case: Large-scale servers (>10,000 connections).
	•	Advantages: Mature, optimized for high concurrency.
	•	Limitations: Larger binary (~10MB), external dependency.
Example:
```cpp
class AsioNetworkBackend : public NetworkBackend {
public:
    std::future accept_connections(int port) override {
        asio::io_context ctx;
        // Setup acceptor and handle connections
        co_return;
    }
};
```
## Usage
### C++ API
The Server class provides a simple interface to initialize and run the server.
Example:
```cpp
#include 
#include 

int main() {
    stellane::Router router;
    router.get("/api/users", [](const Request& req) -> Response {
        return Response{200, "{\"users\": []}"};
    });

    stellane::Server server(8080, std::make_unique());
    server.route(router);
    server.run();
    return 0;
}
```
Key Methods:
Method
Description
Server(int port, std::unique_ptr)
Initializes the server with a port and backend.
route(Router&)
Binds the router for request dispatching.
run()
Starts the server, listening for connections.
### CLI Integration
The stellane run command executes the server binary, loading .env and stellane.template.toml.
Synopsis:
stellane run [--docker] [--config ] [--log-level ]
Example:
cd my-game-server
stellane run
# Output:
# Starting server on http://localhost:8080
# Logs written to ~/.stellane/logs/my-game-server.log

## Cross-Platform Considerations
  •	Supported Platforms: Windows (WinSock2), Linux (epoll), macOS (kqueue).
	•	Socket Handling:
	◦	STL: Platform-specific (select, WSAWaitForMultipleEvents, epoll, kqueue).
	◦	Unifex: Abstracts platform differences via unifex::io_context.
	◦	Boost.Asio: Built-in abstraction.
	•	Path Handling: Uses std::filesystem for consistent paths.
	•	Permissions: Logs and configs stored in ~/.stellane (0700 directories, 0600 files).

## Security Mechanisms
  •	No sudo: Runs on non-privileged ports (e.g., 8080).
	•	Memory Safety: Uses unique_ptr/shared_ptr, std::expected for type-safe errors.
	•	TLS Support: Optional OpenSSL or platform-native (SChannel, Secure Transport).
	•	Logging: Secure logs in ~/.stellane/logs (0600 permissions).
	•	Isolation: All operations confined to user home directory.

## Performance Characteristics
Backend
Connections
Latency
Binary Size
STL
~1,000
<10ms
~1MB
Unifex
~5,000
<5ms
~2-3MB
Boost.Asio
>10,000
<2ms
~10MB
	•	Optimization: Co-routines for async I/O, std::vector for buffers.
	•	Scalability: NetworkBackend allows switching to Boost.Asio for large-scale needs.

## Examples
Running a REST Server
```bash
stellane new my-game-server --template authserver-rest-jwt
cd my-game-server
stellane run
# Output:
# Starting server on http://localhost:8080
```
Test:
```bash
curl http://localhost:8080/api/users
# Output: {"users": []}
```
Configuring TLS
Edit .env:
```
TLS_ENABLED=true
TLS_CERT_PATH=/path/to/cert.pem
TLS_KEY_PATH=/path/to/key.pem
```
Run:
```bash
stellane run
# Output:
# Starting server on https://localhost:8080
```
Using Unifex Backend
Edit stellane.template.toml:
```
[server]
port = 8080
backend = "unifex"

Run:
stellane run
```
## Troubleshooting
Issue
Solution
Port already in use
Change PORT in .env or stop conflicting process.
Backend not found
Verify NETWORK_BACKEND in .env (stl, unifex, asio).
TLS configuration failed
Ensure TLS_CERT_PATH and TLS_KEY_PATH are valid.
Connection refused
Check server logs (~/.stellane/logs/.log).
Logs: Check ~/.stellane/logs/.log for detailed errors.

## References
  •	Quickstart Guide
	•	Template Registry Specification
	•	manifest.json Specification
	•	Router Specification
	•	Contributing Guide
	•	GitHub Repository

## License
MIT License
