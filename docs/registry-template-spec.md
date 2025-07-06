# Stellane Template Registry Specification

The Stellane Template Registry is a centralized system for managing prebuilt, production-ready C++ backend templates (e.g., `authserver-rest-jwt`, `posts-crud`) used by the Stellane CLI (`stellane new`, `stellane list-templates`). It ensures **high performance**, **exceptional developer experience (DX)**, and **ironclad security** by providing verified, reusable templates for game servers and real-time applications.

This document outlines the structure, behavior, and requirements of the template registry, including the `manifest.json` schema, security mechanisms, caching strategy, and guidelines for creating templates.

---

## Table of Contents
1. [Overview](#overview)
2. [Registry Structure](#registry-structure)
   - [manifest.json Schema](#manifestjson-schema)
   - [Template Package Format](#template-package-format)
3. [Security Mechanisms](#security-mechanisms)
   - [SHA256 Checksum](#sha256-checksum)
   - [GPG Signature (Optional)](#gpg-signature-optional)
4. [Caching Strategy](#caching-strategy)
5. [Template Creation Guidelines](#template-creation-guidelines)
   - [stellane.template.toml](#stellanetemplatetoml)
   - [Directory Structure](#directory-structure)
6. [CLI Integration](#cli-integration)
   - [stellane new](#stellane-new)
   - [stellane list-templates](#stellane-list-templates)
7. [Cross-Platform Considerations](#cross-platform-considerations)
8. [Extending the Registry](#extending-the-registry)
9. [Examples](#examples)
10. [Contributing](#contributing)

---

## Overview

The Stellane Template Registry is a distributed system hosted on GitHub (or custom registries) that stores metadata about available templates in a `manifest.json` file. Templates are prebuilt C++ projects (e.g., REST servers, game backends) packaged as `.tar.gz` archives, verified with SHA256 checksums and optional GPG signatures. The Stellane CLI uses this registry to download, verify, and set up projects with zero compilation time, ensuring **performance**, **DX**, and **security**.

Key features:
- **Performance**: Cached `manifest.json` (1-hour TTL) and template archives reduce network overhead.
- **DX**: Intuitive CLI commands, dynamic configuration via `stellane.template.toml`.
- **Security**: No `sudo`, 0700/0600 permissions, SHA256/GPG verification.
- **Cross-Platform**: Supports Windows (MSVC), Linux (GCC/Clang), macOS (Clang, ARM64).

---

## Registry Structure

### manifest.json Schema

The `manifest.json` file, hosted at a central registry (e.g., `https://raw.githubusercontent.com/stellane/releases/main/manifest.json`), defines available templates, their versions, and metadata.

**Schema**:
```json
{
  "registry_version": "1.0",
  "templates": {
    "": {
      "description": "",
      "stellane_cli_compat": "",
      "version_tags": {
        "": "",
        "latest": "",
        "stable": ""
      },
      "versions": {
        "": {
          "url": "",
          "checksum_sha256": "",
          "signature_gpg_url": "",
          "created_at": "",
          "dependencies": ["", ...]
        }
      }
    }
  }
}
Example:
{
  "registry_version": "1.0",
  "templates": {
    "authserver-rest-jwt": {
      "description": "RESTful API server with JWT authentication",
      "stellane_cli_compat": "^0.2.0",
      "version_tags": {
        "latest": "1.0.1",
        "stable": "1.0.0"
      },
      "versions": {
        "1.0.1": {
          "url": "https://github.com/stellane/releases/download/v1.0.1/authserver-rest-jwt-1.0.1.tar.gz",
          "checksum_sha256": "a1b2c3d4...",
          "signature_gpg_url": "https://github.com/stellane/releases/download/v1.0.1/authserver-rest-jwt-1.0.1.tar.gz.sig",
          "created_at": "2025-07-01T12:00:00Z",
          "dependencies": ["libpqxx", "openssl"]
        }
      }
    }
  }
}
```
	•	registry_version: Schema version for backward compatibility.
	•	templates: Map of template names to metadata.
	•	stellane_cli_compat: SemVer range for CLI compatibility.
	•	version_tags: Aliases (e.g., latest, stable) for versions.
	•	versions: Version-specific metadata (URL, checksum, signature).
	•	dependencies: Required libraries (e.g., libpqxx).
### Template Package Format
Templates are distributed as .tar.gz archives containing:
	•	stellane.template.toml: Template configuration (variables, scripts, binaries).
	•	src/: C++ source files (optional, for custom builds).
	•	bin/: Prebuilt binaries for supported platforms (Windows, Linux, macOS).
	•	docs/: Documentation (e.g., QUICKSTART.md).
	•	.env.example: Sample environment variables.
Example Structure:
```
authserver-rest-jwt-1.0.1.tar.gz
├── stellane.template.toml
├── bin/
│   ├── linux-x86_64/authserver
│   ├── windows-x86_64/authserver.exe
│   ├── macos-arm64/authserver
├── src/
│   ├── main.cpp
│   ├── handlers/
├── docs/
│   ├── QUICKSTART.md
├── .env.example
```
### Security Mechanisms
SHA256 Checksum
	•	Every template archive is verified using a SHA256 checksum to ensure integrity.
	•	Checksum is stored in manifest.json (checksum_sha256).
	•	CLI rejects templates with mismatched checksums, deleting corrupted files.
Implementation:
hasher = hashlib.sha256()
with open(cached_archive_path, 'rb') as f:
    while chunk := f.read(8192):
        hasher.update(chunk)
if hasher.hexdigest() != expected_checksum:
    raise click.ClickException("Checksum mismatch!")
GPG Signature (Optional)
	•	Templates can include a GPG signature (.sig file) for authenticity.
	•	CLI verifies signatures using python-gnupg (if installed).
	•	If GPG is unavailable, a warning is issued, and verification is skipped.
Implementation:
gpg = gnupg.GPG()
sig_res = requests.get(sig_url)
verified = gpg.verify_file(open(cached_archive_path, "rb"), data=sig_res.content)
if not verified:
    raise click.ClickException("GPG signature verification failed!")
	•	Security Guarantees:
	◦	No sudo required; all operations in ~/.stellane (0700/0600 permissions).
	◦	User home directory (~/.stellane/cache) for caching, ensuring isolation.

Caching Strategy
	•	Location: ~/.stellane/cache (0700 permissions).
	•	Files:
	◦	manifest.json: Cached with 1-hour TTL (3600 seconds).
	◦	Template archives (.tar.gz): Cached indefinitely, reused for --offline mode.
	•	Cleanup: stellane cache clean command (proposed) removes outdated files.
	•	Performance: Reduces network requests, supports offline workflows.
	•	Security: Files stored with 0600 permissions, no system-wide access.
Implementation:
CACHE_DIR = Path.home() / ".stellane" / "cache"
if (time.time() - manifest_path.stat().st_mtime) <= MANIFEST_CACHE_TTL_SECONDS:
    return json.loads(manifest_path.read_text())

Template Creation Guidelines
stellane.template.toml
The stellane.template.toml file defines template configuration, including environment variables, scripts, and binaries.
Schema:
```
[template]
name = ""
version = ""
description = ""

[variables]
 = { prompt = "", default = "", secret =  }

[scripts]
build = ""
run = ""

[binaries]
"" = ""
Example:
[template]
name = "authserver-rest-jwt"
version = "1.0.1"
description = "RESTful API server with JWT authentication"

[variables]
PORT = { prompt = "Enter the server port", default = "8080", secret = false }
DATABASE_URL = { prompt = "Enter the database connection URL", default = "", secret = true }
JWT_SECRET = { prompt = "Enter the JWT secret key", default = "", secret = true }

[scripts]
build = "cmake -B build && cmake --build build"
run = "bin/{platform}/authserver --config config/default.toml"

[binaries]
linux-x86_64 = "bin/linux-x86_64/authserver"
windows-x86_64 = "bin/windows-x86_64/authserver.exe"
macos-arm64 = "bin/macos-arm64/authserver"
```
	•	variables: User-configurable environment variables, with prompts and secret flags.
	•	scripts: Commands for building/running the template (optional for prebuilt).
	•	binaries: Platform-specific binary paths.
### Directory Structure
Templates must follow a consistent structure to ensure compatibility:
-.tar.gz
```
├── stellane.template.toml
├── bin/
│   ├── /
├── src/ (optional)
│   ├── 
├── docs/
│   ├── QUICKSTART.md
├── .env.example
├── config/ (optional)
│   ├── default.toml
```
	•	bin/: Prebuilt binaries for supported platforms.
	•	src/: Source code for custom builds (optional).
	•	docs/: User guides (e.g., QUICKSTART.md).
	•	.env.example: Sample .env file with required variables.

## CLI Integration
stellane new
	•	Command: stellane new --template [--version ] [--offline]
	•	Workflow:
	1	Fetch manifest.json (cached or remote).
	2	Download template archive (verify SHA256/GPG).
	3	Extract to .
	4	Prompt for [variables] in stellane.template.toml.
	5	Generate .env (0600) and .stellane/project.json.
	•	Example: stellane new my-game-server --template authserver-rest-jwt
	•	
stellane list-templates
	•	Command: stellane list-templates [--offline]
	•	Workflow: Display available templates from cached/remote manifest.json.
	•	Example Output: Available templates:
	•	- authserver-rest-jwt (latest: 1.0.1): RESTful API server with JWT authentication
	•	- posts-crud (latest: 1.0.0): Simple CRUD server for blog posts
	•	

Cross-Platform Considerations
	•	Supported Platforms: Windows (MSVC), Linux (GCC/Clang), macOS (Clang, ARM64).
	•	File Permissions: 0700 for directories, 0600 for files (~/.stellane, .env, project.json).
	•	Path Handling: Uses std::filesystem (C++) and pathlib.Path (Python).
	•	Binaries: Templates include prebuilt binaries for each platform in bin/.
	•	Implementation:
	◦	Windows: Skips os.chmod for permissions.
	◦	Linux/macOS: Applies 0700/0600 permissions.

Extending the Registry
	•	Custom Registries: Configure in ~/.stellane/config.toml: [registry]
	•	custom = ["https://my-custom-registry.com/manifest.json"]
	•	
	•	Community Registries: Planned support for CDN-hosted or decentralized registries.
	•	GitHub Actions: Automate manifest.json updates and template releases.

Examples
Creating a New Project
stellane new my-game-server --template authserver-rest-jwt
# Prompts:
# Enter the server port [8080]:
# Enter the database connection URL []: postgres://user:pass@localhost:5432/db
# Enter the JWT secret key [hidden]:
Resulting Files
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
stellane.template.toml Example
```
[template]
name = "authserver-rest-jwt"
version = "1.0.1"

[variables]
PORT = { prompt = "Enter the server port", default = "8080", secret = false }
DATABASE_URL = { prompt = "Enter the database connection URL", default = "", secret = true }

[scripts]
run = "bin/{platform}/authserver --config config/default.toml"

[binaries]
linux-x86_64 = "bin/linux-x86_64/authserver"
```
Contributing
We welcome contributions to the Stellane Template Registry! To add a new template:
	1	Create a .tar.gz archive with the required structure.
	2	Generate SHA256 checksum and (optionally) GPG signature.
	3	Update manifest.json in the central registry.
	4	Submit a pull request to stellane/releases.
See CONTRIBUTING.md for details.

License
MIT License
