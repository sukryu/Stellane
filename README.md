# Stellane

🚀 **Stellane**: The ultimate C++20/23/26 backend framework for game servers and real-time applications. Built for **extreme performance**, **unmatched developer experience (DX)**, and **ironclad security**, Stellane empowers developers to create scalable, production-ready backends with zero compilation time.

## Why Stellane?
- **Blazing Fast**: Native C++ with prebuilt templates, leveraging Co-routines, `std::expected`, and Concepts for low-latency, high-throughput servers.
- **Developer-Friendly**: Intuitive CLI (`stellane new`, `run`, `analyze`), dynamic templates (`authserver-rest-jwt`, `posts-crud`), and seamless configuration via `.env` and `stellane.template.toml`.
- **Secure by Design**: No `sudo`, minimal permissions (0700/0600), memory safety with `unique_ptr`/`shared_ptr`, and SHA256/GPG-verified templates.
- **Cross-Platform**: Runs on Windows (MSVC), Linux (GCC/Clang), and macOS (Clang, ARM64) with minimal dependencies.
- **Flexible Networking**: STL-based core with Unifex for lightweight async, optional Boost.Asio for massive scale (>10,000 connections).

## Key Features
- **High-Performance Networking**: HTTP/WebSocket servers with C++20 Co-routines and Unifex for real-time applications.
- **Zenix++ ORM**: Type-safe database operations with PostgreSQL, powered by `shared_ptr` connection pooling.
- **Prebuilt Templates**: Kickstart projects with production-ready templates, configured via `stellane.template.toml`.
- **Secure CLI**: Create projects (`stellane new`), run servers (`stellane run`), and analyze performance (`stellane analyze`) without root privileges.
- **Extensible**: Modular design with `NetworkBackend` for STL/Unifex/Asio, supporting custom registries and Docker.

## Get Started
```bash
# Create a new project
stellane new my-game-server --template authserver-rest-jwt

# Run your server
cd my-game-server
stellane run
Why Choose Stellane?
Unlike NestJS (JavaScript, slow runtime), Spring (heavy JVM), or Rust (limited templates), Stellane delivers native performance, lightweight binaries, and a rich template ecosystem tailored for game servers and real-time apps.
Join us to redefine C++ backend development! 🌟
Contributing
We welcome contributions! Check out our Contributing Guide and join the Stellane community.
License
MIT License
---
