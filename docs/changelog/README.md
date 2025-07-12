# Stellane Changelog

This directory contains the complete changelog for the Stellane C++ backend framework. Our changelog follows industry best practices from projects like Kubernetes, React, and TypeScript to ensure clear communication of changes to developers and system architects.

## 📋 Table of Contents

- [Directory Structure](#directory-structure)
- [Changelog Format](#changelog-format)
- [Change Categories](#change-categories)
- [Semantic Versioning](#semantic-versioning)
- [Breaking Changes Policy](#breaking-changes-policy)
- [Performance Impact Guidelines](#performance-impact-guidelines)
- [Contributing](#contributing)
- [Migration Guides](#migration-guides)

## 📁 Directory Structure

```
docs/changelog/
├── README.md                         # This file - changelog guidelines
├── UNRELEASED.md                     # Upcoming changes for next release
├── v0.1.0.md                        # Version-specific changelogs
├── v0.2.0.md
├── v1.0.0.md
├── migration/                        # Version migration guides
│   ├── v0.1-to-v0.2.md
│   └── v0.2-to-v1.0.md
├── breaking-changes/                 # Detailed breaking change analysis
│   ├── 2025-01-router-api.md
│   └── 2025-02-context-refactor.md
└── templates/                       # Standard format templates
    ├── changelog-entry.md
    └── breaking-change.md
```

## 🎯 Changelog Format

Each version changelog follows this structure:

```markdown
# Stellane v1.2.0 (2025-07-15)

## 🚀 Features
- **Router**: Added Patricia Trie-based dynamic routing with O(k) complexity
- **Context**: Introduced hierarchical locking for deadlock prevention
- **CLI**: New `stellane analyze` command for performance profiling

## ⚡ Performance
- **Routing**: 60% faster lookup times for dynamic routes (10k+ routes)
- **Memory**: Reduced memory usage by 40% in Patricia Trie implementation
- **I/O**: io_uring backend now supports 500K+ concurrent connections

## 🐛 Bug Fixes
- **Context**: Fixed race condition in context propagation across coroutines
- **Router**: Resolved memory leak in route parameter extraction
- **Logging**: Fixed deadlock in AsyncSink under high concurrency

## 💥 BREAKING CHANGES
- **Router API**: Changed `router.mount()` signature to support nested routing
- **Context**: Removed deprecated `ctx.get_unsafe()` method
- **Task**: Modified Task<T> constructor to require explicit coroutine handle

## 📚 Documentation
- Added comprehensive routing performance benchmarks
- Updated Context API reference with deadlock prevention patterns
- New tutorial: Building real-time chat servers with Stellane

## 🔧 Internal
- Refactored Patricia Trie node allocation for better cache locality
- Improved CI/CD pipeline with cross-platform testing
- Added extensive benchmarking suite for routing algorithms
```

## 🏷️ Change Categories

### Primary Categories

- 🚀 **Features**: New functionality, APIs, or capabilities
- ⚡ **Performance**: Speed improvements, memory optimizations, scalability enhancements
- 🐛 **Bug Fixes**: Fixes for crashes, memory leaks, race conditions, or incorrect behavior
- 💥 **BREAKING CHANGES**: API changes that require user code modifications
- 📚 **Documentation**: README updates, API docs, tutorials, examples
- 🔧 **Internal**: Refactoring, code quality, tooling improvements

### Stellane-Specific Categories

- 🎯 **CLI**: Changes to `stellane` command-line tool
- 🌐 **Templates**: Updates to template registry and prebuilt templates
- 🔄 **Runtime**: Async runtime, event loops, task scheduling
- 🛣️ **Routing**: Router, Patricia Trie, parameter injection
- 📝 **Context**: Request-scoped context and logging
- 🔒 **Security**: Authentication, authorization, vulnerability fixes

## 📦 Semantic Versioning

Stellane follows [Semantic Versioning 2.0.0](https://semver.org/):

- **MAJOR** (X.0.0): Breaking changes to public APIs
- **MINOR** (0.X.0): New features, backward-compatible additions
- **PATCH** (0.0.X): Bug fixes, security patches, documentation

### Pre-1.0 Versioning

During the pre-1.0 phase:

- **0.X.0**: May include breaking changes (carefully documented)
- **0.0.X**: Bug fixes and minor improvements
- All breaking changes are clearly marked and include migration guides

## 💥 Breaking Changes Policy

### Definition

A breaking change is any modification that requires users to update their code, configuration, or deployment processes.

### Examples of Breaking Changes

- Removing or renaming public APIs
- Changing function signatures or return types
- Modifying default behavior
- Requiring new dependencies or minimum versions
- Changing CLI command structure

### Documentation Requirements

Each breaking change must include:

1. **Migration Guide**: Step-by-step instructions for updating code
1. **Rationale**: Why the change was necessary
1. **Timeline**: Deprecation period and removal schedule
1. **Code Examples**: Before/after comparisons
1. **Impact Assessment**: Affected components and user groups

### Example Breaking Change Entry

```markdown
## 💥 BREAKING CHANGES

### Router API Redesign (`router.mount()`)

**Why**: The previous API couldn't support nested routing patterns required for microservice architectures.

**Migration**:
```cpp
// Before (v0.1.x)
router.mount("/api", sub_router);

// After (v0.2.x)
router.mount("/api", sub_router, RouterOptions{.prefix_handling = true});
```

**Impact**: All applications using `router.mount()` (estimated 80% of Stellane projects)

**Timeline**:

- v0.2.0: New API introduced, old API deprecated with warnings
- v0.3.0: Old API removed

**See**: [Migration Guide](migration/v0.1-to-v0.2.md#router-api-changes)

```
## ⚡ Performance Impact Guidelines

Given Stellane's focus on high-performance applications, all performance-related changes must include:

### Benchmarking Requirements
- **Methodology**: Description of benchmark setup and environment
- **Metrics**: Latency (P50, P95, P99), throughput (RPS), memory usage
- **Comparison**: Before/after performance data
- **Hardware**: CPU, memory, storage specifications used for testing

### Example Performance Entry
```markdown
## ⚡ Performance

### Patricia Trie Routing Optimization

**Improvement**: 60% faster dynamic route lookup for applications with 10K+ routes

**Benchmarks**:
- Hardware: Intel Xeon 16-core, 64GB RAM, NVMe SSD
- Test: 100K requests across 10K unique dynamic routes
- Before: P99 latency 15ms, 50K RPS
- After: P99 latency 6ms, 120K RPS
- Memory: 40% reduction in heap allocation per lookup

**Technical Details**:
- Replaced recursive matching with iterative algorithm
- Improved cache locality through node memory layout optimization
- Added LRU cache for frequently accessed routes (95% hit rate)

**See**: [Benchmark Details](../benchmarks/routing-v0.2.0.md)
```

## 📖 Migration Guides

### Purpose

Migration guides help users upgrade between major and minor versions with minimal friction.

### Structure

Each migration guide includes:

1. **Overview**: High-level summary of changes
1. **Prerequisites**: Required tools, dependencies, backup procedures
1. **Step-by-Step Instructions**: Detailed upgrade process
1. **Code Updates**: Required changes with examples
1. **Validation**: How to verify successful migration
1. **Troubleshooting**: Common issues and solutions
1. **Rollback**: How to revert if needed

### Example Migration Guide Structure

```markdown
# Migration Guide: v0.1.x to v0.2.x

## Overview
Version 0.2.0 introduces the new Patricia Trie routing system and hierarchical context locking.

## Prerequisites
- Stellane v0.1.5 or later
- C++20 compatible compiler (GCC 10+, Clang 12+, MSVC 19.29+)
- Backup your existing project

## Breaking Changes Summary
1. Router API changes (`mount()` signature)
2. Context locking hierarchy
3. Task<T> constructor modifications

## Step 1: Update Router Usage
[Detailed steps...]

## Step 2: Migrate Context Usage
[Detailed steps...]

## Validation
[Test procedures...]
```

## ✏️ Contributing

### Adding Changelog Entries

1. **For Unreleased Changes**: Add entries to `UNRELEASED.md`
1. **For New Releases**: Move entries from `UNRELEASED.md` to version-specific file
1. **For Breaking Changes**: Create detailed analysis in `breaking-changes/`

### Style Guidelines

- Use clear, concise language
- Include code examples for API changes
- Link to relevant documentation and issues
- Follow the emoji conventions for categories
- Include performance data for optimization changes

### Review Process

All changelog entries undergo review for:

- Technical accuracy
- Completeness of migration information
- Clarity for end users
- Consistency with established format

## 🔗 Related Documentation

- [API Reference](../reference/) - Complete API documentation
- [Architecture Guide](../internals/) - Internal system design
- [Performance Benchmarks](../benchmarks/) - Detailed performance analysis
- [Contributing Guide](../../CONTRIBUTING.md) - How to contribute to Stellane

-----

**Note**: This changelog format is inspired by industry leaders like Kubernetes, React, and TypeScript, adapted for Stellane’s high-performance C++ backend framework requirements.
