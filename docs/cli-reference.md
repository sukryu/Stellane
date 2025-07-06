# Stellane CLI Reference

The **Stellane CLI** is the primary interface for interacting with the Stellane Ecosystem, a high-performance C++20/23/26 backend framework for game servers and real-time applications. It provides commands to create projects, run servers, manage templates, and analyze performance, ensuring **blazing-fast performance**, **exceptional developer experience (DX)**, and **ironclad security**.

This reference details all Stellane CLI commands, their options, and usage examples, following the principles of no `sudo`, cross-platform compatibility, and minimal dependencies.

---

## Table of Contents
- [Overview](#overview)
- [Installation](#installation)
- [Command Reference](#command-reference)
  - [stellane new](#stellane-new)
  - [stellane run](#stellane-run)
  - [stellane list-templates](#stellane-list-templates)
  - [stellane cache clean](#stellane-cache-clean)
  - [stellane analyze](#stellane-analyze)
- [Global Options](#global-options)
- [Environment Variables](#environment-variables)
- [Configuration Files](#configuration-files)
- [Troubleshooting](#troubleshooting)
- [References](#references)

---

## Overview

The Stellane CLI is a Python-based tool (`click` framework) that streamlines project creation, execution, and management for Stellane projects. It integrates with the [Template Registry](registry-template-spec.md) and [`manifest.json`](manifest-json-spec.md) to provide prebuilt, production-ready templates (e.g., `authserver-rest-jwt`, `posts-crud`). Key features include:

- **Performance**: Cached `manifest.json` and templates (`~/.stellane/cache`) minimize network requests.
- **DX**: Intuitive commands, colorized output (`click.secho`), and progress bars (`tqdm` proposed).
- **Security**: No `sudo`, 0700/0600 permissions for `~/.stellane`, SHA256/GPG verification.
- **Cross-Platform**: Supports Windows (MSVC), Linux (GCC/Clang), macOS (Clang, ARM64) via `pathlib.Path`.

---

## Installation

Install the Stellane CLI using `pip`:

```bash
pip install stellane
Verify installation:
stellane --version
# Output: stellane, version 0.2.0
```
```
Note: Use pip install --user stellane to avoid system-wide permissions, aligning with Stellane’s security model. Ensure ~/.local/bin (Linux/macOS) or %USERPROFILE%\AppData\Roaming\Python\Scripts (Windows) is in your PATH.
```
Prerequisites:
	•	Python 3.8+
	•	Optional: libpqxx (for Zenix++ ORM), openssl (for TLS), python-gnupg (for GPG verification).

## Command Reference
### stellane new
Create a new Stellane project from a template.
Synopsis:
stellane new  --template  [--version ] [--offline] [--force]
Options:
Option
Description

Name of the project directory (e.g., my-game-server).
--template
Template name (e.g., authserver-rest-jwt). See list-templates.
--version
Template version or tag (e.g., 1.0.1, @latest). Default: @latest.
--offline
Use cached templates (~/.stellane/cache). Skips network requests.
--force
Overwrite existing project directory. Use with caution.
Description:
	•	Fetches manifest.json from the registry or cache.
	•	Downloads and verifies the template archive (SHA256, optional GPG).
	•	Extracts to , prompts for variables (stellane.template.toml), and generates .env (0600 permissions).
	•	Creates .stellane/project.json for project metadata.
Example:
stellane new my-game-server --template authserver-rest-jwt
# Prompts:
# Enter the server port [8080]:
# Enter the database connection URL []: postgres://user:pass@localhost:5432/db
# Enter the JWT secret key [hidden]: my-secret-key
# Output:
# Project created at my-game-server/
Files Generated:
```
my-game-server/
├── .env
├── .stellane/
│   ├── project.json
├── bin/
│   ├── linux-x86_64/authserver
├── docs/
│   ├── QUICKSTART.md
├── config/
│   ├── default.toml
```
### stellane run
Run a Stellane project server.
Synopsis:
stellane run [--docker] [--config ] [--log-level ]
Options:
Option
Description
--docker
Run the server in a Docker container (if supported by the template).
--config
Path to a custom configuration file (default: config/default.toml).
--log-level
Logging level (debug, info, warn, error). Default: info.
Description:
	•	Loads .env variables and stellane.template.toml [scripts.run].
	•	Executes the platform-specific binary (e.g., bin/linux-x86_64/authserver).
	•	Writes logs to ~/.stellane/logs/.log (0600 permissions).
	•	Supports Docker execution for containerized environments.
Example:
cd my-game-server
stellane run
# Output:
# Starting server on http://localhost:8080
# Logs written to ~/.stellane/logs/my-game-server.log
Docker Example:
stellane run --docker
# Output:
# Starting Docker container for my-game-server...
# Server running on http://localhost:8080

### stellane list-templates
List available templates from the registry.
Synopsis:
stellane list-templates [--offline]
Options:
Option
Description
--offline
Use cached manifest.json (~/.stellane/cache). Skips network requests.
Description:
	•	Fetches manifest.json from the registry or cache.
	•	Displays template names, versions, and descriptions.
Example:
stellane list-templates
# Output:
# Available templates:
# - authserver-rest-jwt (latest: 1.0.1): RESTful API server with JWT authentication
# - posts-crud (latest: 1.0.0): Simple CRUD server for blog posts

### stellane cache clean
Clear the Stellane cache.
Synopsis:
stellane cache clean [--all] [--manifest] [--templates]
Options:
Option
Description
--all
Clear all cache (~/.stellane/cache). Default if no options specified.
--manifest
Clear only manifest.json.
--templates
Clear only template archives (.tar.gz).
Description:
	•	Removes cached manifest.json and/or template archives.
	•	Maintains ~/.stellane/cache permissions (0700 directories, 0600 files).
Example:
stellane cache clean --manifest
# Output:
# Cleared cached manifest.json

### stellane analyze
Analyze project performance or configuration (proposed).
Synopsis:
stellane analyze [--type ] [--output ]
Options:
Option
Description
--type
Analysis type (performance, config, dependencies). Default: performance.
--output
Output file for analysis results. Default: stdout.
Description:
	•	Analyzes server performance (e.g., latency, throughput) or configuration validity.
	•	Logs results to ~/.stellane/logs/analyze-.log or specified file.
Example:
stellane analyze --type performance
# Output:
# Analyzing server at http://localhost:8080...
# Average latency: 4.2ms
# Throughput: 1200 requests/second
Note: This command is proposed and may require additional server instrumentation.

## Global Options
Option
Description
--help
Show help for any command (e.g., stellane new --help).
--version
Display CLI version (e.g., stellane --version).
--verbose
Enable verbose output for debugging.
Example:
stellane new my-game-server --template authserver-rest-jwt --verbose
# Output: Detailed logs of manifest fetching, download, and verification

## Environment Variables
The CLI uses environment variables for configuration, loaded from .env or system environment.
Variable
Description
STELLANE_REGISTRY_URL
Custom registry URL (default: https://raw.githubusercontent.com/stellane/releases/main/manifest.json).
STELLANE_CACHE_DIR
Cache directory (default: ~/.stellane/cache).
STELLANE_LOG_DIR
Log directory (default: ~/.stellane/logs).
Example:
export STELLANE_REGISTRY_URL=https://my-custom-registry.com/manifest.json
stellane new my-game-server --template authserver-rest-jwt

## Configuration Files
	•	~/.stellane/config.toml: Global CLI configuration (e.g., custom registries). [registry]
	•	custom = ["https://my-custom-registry.com/manifest.json"]
	•	
	•	stellane.template.toml: Template-specific configuration (see Template Specification).
	•	.env: Project-specific environment variables (0600 permissions). PORT=8080
	•	DATABASE_URL=postgres://user:pass@localhost:5432/db
	•	

## Troubleshooting
Issue
Solution
stellane: command not found
Add ~/.local/bin (Linux/macOS) or %USERPROFILE%\AppData\Roaming\Python\Scripts (Windows) to PATH.
Checksum mismatch
Run stellane cache clean --templates and retry.
GPG verification failed
Ensure python-gnupg is installed or skip with --offline.
Database connection failed
Verify DATABASE_URL in .env and ensure PostgreSQL is running.
Permission denied
Check ~/.stellane permissions (0700 for directories, 0600 for files).
Logs: Check ~/.stellane/logs/.log for detailed errors.

## References
	•	Template Registry Specification
	•	manifest.json Specification
	•	Quickstart Guide
	•	Contributing Guide
	•	GitHub Repository

## License
MIT License
