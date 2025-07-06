# Stellane manifest.json Specification

The `manifest.json` file is the cornerstone of the Stellane Template Registry, providing metadata for prebuilt C++ backend templates (e.g., `authserver-rest-jwt`, `posts-crud`) used by the Stellane CLI (`stellane new`, `stellane list-templates`). It enables **high performance**, **exceptional developer experience (DX)**, and **ironclad security** by defining template versions, download URLs, checksums, and compatibility requirements.

This document details the structure, fields, and usage of `manifest.json`, including its schema, security mechanisms, and guidelines for creation and maintenance.

---

## Table of Contents
1. [Overview](#overview)
2. [manifest.json Schema](#manifestjson-schema)
   - [Top-Level Fields](#top-level-fields)
   - [Template Object](#template-object)
   - [Version Object](#version-object)
3. [Security Mechanisms](#security-mechanisms)
   - [SHA256 Checksum](#sha256-checksum)
   - [GPG Signature (Optional)](#gpg-signature-optional)
4. [Usage in Stellane CLI](#usage-in-stellane-cli)
   - [Fetching and Caching](#fetching-and-caching)
   - [Template Selection](#template-selection)
5. [Creating and Updating manifest.json](#creating-and-updating-manifestjson)
6. [Cross-Platform Considerations](#cross-platform-considerations)
7. [Extending the Registry](#extending-the-registry)
8. [Examples](#examples)
9. [Contributing](#contributing)
10. [License](#license)

---

## Overview

The `manifest.json` file is a JSON document hosted at a central registry (e.g., `https://raw.githubusercontent.com/stellane/releases/main/manifest.json`) or custom registries. It lists available templates, their versions, download URLs, checksums, and CLI compatibility requirements. The Stellane CLI uses this file to discover, download, and verify templates, ensuring **zero compilation time**, **secure execution**, and **seamless developer experience**.

**Key Features**:
- **Performance**: Cached with a 1-hour TTL (`~/.stellane/cache`, 0600 permissions) to minimize network requests.
- **DX**: SemVer-based versioning (`@latest`, `@stable`), clear metadata for templates.
- **Security**: SHA256 checksums and optional GPG signatures for integrity and authenticity.
- **Cross-Platform**: Supports templates for Windows (MSVC), Linux (GCC/Clang), and macOS (Clang, ARM64).

---

## manifest.json Schema

The `manifest.json` file follows a strict JSON schema to ensure consistency and compatibility.

### Top-Level Fields
| Field              | Type   | Required | Description                                                                 |
|--------------------|--------|----------|-----------------------------------------------------------------------------|
| `registry_version` | String | Yes      | Schema version (e.g., `"1.0"`) for backward compatibility.                  |
| `templates`        | Object | Yes      | Map of template names to their metadata (see [Template Object](#template-object)). |

### Template Object
Each template is a key-value pair in the `templates` object, where the key is the template name (e.g., `authserver-rest-jwt`) and the value is an object with the following fields:

| Field                  | Type   | Required | Description                                                                 |
|------------------------|--------|----------|-----------------------------------------------------------------------------|
| `description`          | String | Yes      | Brief description of the template (e.g., `"RESTful API with JWT"`).         |
| `stellane_cli_compat`  | String | Yes      | SemVer range for CLI compatibility (e.g., `"^0.2.0"`).                      |
| `version_tags`         | Object | Yes      | Map of tags (e.g., `latest`, `stable`) to specific versions (e.g., `"1.0.1"`). |
| `versions`             | Object | Yes      | Map of version strings to version metadata (see [Version Object](#version-object)). |

### Version Object
Each version in the `versions` object contains metadata for a specific template version:

| Field              | Type   | Required | Description                                                                 |
|--------------------|--------|----------|-----------------------------------------------------------------------------|
| `url`              | String | Yes      | Download URL for the `.tar.gz` archive (e.g., GitHub release URL).          |
| `checksum_sha256`  | String | Yes      | SHA256 checksum of the archive for integrity verification.                  |
| `signature_gpg_url`| String | No       | URL for the GPG signature (`.sig`) file (optional).                        |
| `created_at`       | String | Yes      | ISO 8601 timestamp of version creation (e.g., `"2025-07-01T12:00:00Z"`).    |
| `dependencies`     | Array  | No       | List of required libraries (e.g., `["libpqxx", "openssl"]`).                |

**Full Schema Example**:
```json
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
          "checksum_sha256": "a1b2c3d4e5f6...",
          "signature_gpg_url": "https://github.com/stellane/releases/download/v1.0.1/authserver-rest-jwt-1.0.1.tar.gz.sig",
          "created_at": "2025-07-01T12:00:00Z",
          "dependencies": ["libpqxx", "openssl"]
        },
        "1.0.0": {
          "url": "https://github.com/stellane/releases/download/v1.0.0/authserver-rest-jwt-1.0.0.tar.gz",
          "checksum_sha256": "b2c3d4e5f6...",
          "created_at": "2025-06-01T12:00:00Z"
        }
      }
    },
    "posts-crud": {
      "description": "Simple CRUD server for blog posts",
      "stellane_cli_compat": "^0.2.0",
      "version_tags": { "latest": "1.0.0" },
      "versions": {
        "1.0.0": {
          "url": "https://github.com/stellane/releases/download/v1.0.0/posts-crud-1.0.0.tar.gz",
          "checksum_sha256": "c3d4e5f6...",
          "created_at": "2025-06-15T10:00:00Z"
        }
      }
    }
  }
}
```

## Security Mechanisms
SHA256 Checksum
	•	Purpose: Ensures template archive integrity.
	•	Implementation: The CLI computes the SHA256 hash of the downloaded .tar.gz and compares it with checksum_sha256.
	•	Behavior: Mismatched checksums trigger an error, and the corrupted file is deleted.
Example:
```python
import hashlib
hasher = hashlib.sha256()
with open(cached_archive_path, "rb") as f:
    while chunk := f.read(8192):
        hasher.update(chunk)
if hasher.hexdigest() != manifest["templates"][template]["versions"][version]["checksum_sha256"]:
    raise click.ClickException("Checksum mismatch! File corrupted.")
```
GPG Signature (Optional)
	•	Purpose: Verifies authenticity of templates signed by trusted maintainers.
	•	Implementation: The CLI uses python-gnupg to verify the .sig file (if provided).
	•	Behavior: If signature_gpg_url exists, verification is attempted; failure triggers an error. If python-gnupg is unavailable, a warning is issued, and verification is skipped.
Example:
```python
import gnupg
gpg = gnupg.GPG()
sig_res = requests.get(manifest["templates"][template]["versions"][version]["signature_gpg_url"])
verified = gpg.verify_file(open(cached_archive_path, "rb"), data=sig_res.content)
if not verified:
    raise click.ClickException("GPG signature verification failed!")
```
•	Security Guarantees:
◦	No sudo required; all operations in ~/.stellane (0700 directories, 0600 files).
◦	Isolated storage in user home directory prevents system-wide access.

## Usage in Stellane CLI
### Fetching and Caching
	•	Location: ~/.stellane/cache (0700 permissions).
	•	manifest.json:
	◦	Cached with a 1-hour TTL (3600 seconds).
	◦	Refreshed from the registry URL if stale or --offline is not specified.
	•	Template Archives:
	◦	Cached indefinitely, reused for --offline mode.
	◦	Verified with SHA256/GPG before use.
	•	Implementation: CACHE_DIR = Path.home() / ".stellane" / "cache"
	•	manifest_path = CACHE_DIR / "manifest.json"
	•	if manifest_path.exists() and (time.time() - manifest_path.stat().st_mtime) <= 3600:
	•	    return json.loads(manifest_path.read_text())
	•	
### Template Selection
	•	Version Resolution: Users specify templates with optional tags (e.g., authserver-rest-jwt@latest) or versions (e.g., authserver-rest-jwt@1.0.1).
	•	CLI Compatibility: The CLI checks stellane_cli_compat against its version (e.g., 0.2.0) using SemVer.
	•	Example: stellane new my-game-server --template authserver-rest-jwt@latest
	•	

## Creating and Updating manifest.json
To add or update a template in manifest.json:
	1	Create Template Archive:
	◦	Package the template as -.tar.gz (see Template Specification).
	◦	Generate SHA256 checksum: sha256sum authserver-rest-jwt-1.0.1.tar.gz
	◦	
	◦	Optionally, create a GPG signature: gpg --detach-sign --armor authserver-rest-jwt-1.0.1.tar.gz
	◦	
	2	Host Files:
	◦	Upload .tar.gz and .sig (if applicable) to a reliable host (e.g., GitHub Releases).
	3	Update manifest.json:
	◦	Add or update the template entry with url, checksum_sha256, signature_gpg_url, etc.
	◦	Ensure version_tags (e.g., latest, stable) are accurate.
	4	Submit Changes:
	◦	Push updates to the registry repository (e.g., stellane/releases).
	◦	Use GitHub Actions for automated validation and publishing.
Example Workflow:
# Create archive
tar -czf authserver-rest-jwt-1.0.1.tar.gz authserver-rest-jwt/
# Generate checksum
sha256sum authserver-rest-jwt-1.0.1.tar.gz > checksum.txt
# Sign with GPG
gpg --detach-sign --armor authserver-rest-jwt-1.0.1.tar.gz
# Update manifest.json and push

## Cross-Platform Considerations
	•	Supported Platforms: Templates must include binaries for Windows (MSVC), Linux (GCC/Clang), and macOS (Clang, ARM64).
	•	File Permissions: CLI ensures ~/.stellane/cache (0700) and files (0600).
	•	Path Handling: Uses std::filesystem (C++) and pathlib.Path (Python) for consistent paths.
	•	Compatibility: stellane_cli_compat ensures templates work with the user’s CLI version.

## Extending the Registry
	•	Custom Registries: Users can specify alternative registries in ~/.stellane/config.toml: [registry]
	•	custom = ["https://my-custom-registry.com/manifest.json"]
	•	
	•	Future Plans:
	◦	Support for CDN-hosted registries.
	◦	Decentralized registries for community contributions.
	•	Automation: GitHub Actions pipeline for validating and publishing manifest.json updates.

Examples
Full manifest.json
```json
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
          "checksum_sha256": "a1b2c3d4e5f6...",
          "signature_gpg_url": "https://github.com/stellane/releases/download/v1.0.1/authserver-rest-jwt-1.0.1.tar.gz.sig",
          "created_at": "2025-07-01T12:00:00Z",
          "dependencies": ["libpqxx", "openssl"]
        }
      }
    }
  }
}
```
CLI Usage
# List available templates
stellane list-templates
# Output:
# - authserver-rest-jwt (latest: 1.0.1): RESTful API server with JWT authentication

# Create a project
stellane new my-game-server --template authserver-rest-jwt@latest

# Contributing
We welcome contributions to the Stellane Template Registry! To update manifest.json:
	1	Create a new template archive following the Template Specification.
	2	Generate checksums and signatures.
	3	Submit a pull request to stellane/releases.
See CONTRIBUTING.md for details.

# License
MIT Licens
