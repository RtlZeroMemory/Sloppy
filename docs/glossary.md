# Glossary

## Purpose

Define Sloppy terms used across specs, ADRs, implementation tasks, and review prompts. When
a term becomes ambiguous during implementation, update this glossary before relying on an
implicit meaning.

## Sloppy

The project/product name: a TypeScript application runtime with a C host kernel, isolated V8
bridge, Rust compiler tool, and app-host model.

## sloppy

The planned runtime CLI executable.

## sloppyc

The Rust-based compiler/build tool that will parse, transform, extract metadata, and emit
Sloppy artifacts.

## Sloppy Plan

The versioned `app.plan.json` artifact that describes the application graph consumed by the
runtime.

## App Host

The native runtime host that owns lifecycle, services, route dispatch, permissions,
diagnostics, and request flow.

## Runtime Kernel

The C runtime core. It owns process/application lifecycle, memory, resources, diagnostics,
permissions, and native host behavior.

## V8 Bridge

The isolated C++ layer under `src/engine/v8/` that owns V8-specific types and engine
operations.

## Handler ID

A numeric identifier used by the runtime to invoke a JavaScript handler without string
lookup in the hot path.

## App Graph

The complete route, middleware, service, permission, schema, module, job, and health-check
graph of an application.

## Graph Freeze

The point after `builder.build()` when the app graph becomes immutable for static plan mode.

## App Module

A declarative TypeScript module that contributes services, routes, middleware, permissions,
capabilities, jobs, health checks, schemas, or metadata.

## Feature Module

A first-party or third-party module that packages a focused application capability.

## Data Provider

A database provider module such as future SQLite, PostgreSQL, or SQL Server support.

## Resource Table

A runtime table that owns native resources and exposes stable IDs to JavaScript instead of
raw pointers.

## Resource ID

A JS-visible native resource handle composed from table/kind information, slot index, and a
generation counter.

## Generation Counter

A counter used to reject stale resource IDs after slots are closed and reused.

## Capability

A named piece of authority, such as access to a directory or database provider.

## Permission Grant

Configuration or CLI input that grants a capability to an app/module.

## Request Scope

The lifetime boundary for one request, including request-local resources and cleanup.

## JS Worker

A JavaScript execution worker with its own event loop and V8 isolate/context.

## V8 Isolate Owner Thread

The single JS event-loop thread allowed to enter a specific V8 isolate.

## Event Loop

The loop that schedules JS callbacks, microtasks, timers, and native completion messages for
one JS worker.

## Native Completion Queue

A runtime queue used to post native I/O or worker-pool completion messages back to the
owning JS event loop.

## Worker Pool

A bounded native thread pool for blocking or CPU-heavy native operations. It does not run JS
callbacks or enter V8 directly.

## Cancellation Token

A request/job cancellation signal triggered by client disconnects, deadlines, shutdown, or
explicit runtime cancellation.

## Deadline

A configured time limit for a request or operation. Passing the deadline triggers
cancellation and cleanup.

## Backpressure

Limits and flow control that prevent unbounded memory growth, including request body
limits, worker-pool queue limits, DB pool checkout limits, and socket write pressure.

## CPU-Bound JS

JavaScript work that spends significant time computing on the JS thread and can block one JS
worker.

## I/O-Bound Request

A request whose wall time is mostly waiting on network, filesystem, database, timer, or
other async I/O completion.

## Multiple Isolate Scaling

Future scaling model where multiple JS workers each own a separate V8 isolate/event loop.

## Request Arena

An arena allocator used for request-local transient data.

## Static Plan Mode

The optimized mode where the app graph is known before runtime execution.

## Dynamic Mode

An explicit future mode for dynamic graph behavior with reduced optimization and validation.

## Bootstrap Stdlib

The Sloppy-provided JavaScript support library loaded before app code.

## Intrinsic

A runtime-provided primitive exposed through the engine bridge to the bootstrap stdlib.

## Provider-Specific API

An API exposed under a provider namespace for features that are not portable across
providers.

## Native Plugin

A future native extension targeting a versioned Sloppy-owned ABI, not V8 directly.

## Compiler Plugin

A future extension to `sloppyc` for deterministic metadata extraction or validation.

## Windows-First

The current first-class development workflow: Windows x64, `clang-cl`, `lld-link`, CMake,
Ninja, and PowerShell.

## Cross-Platform By Design

The requirement that core runtime architecture remains portable to Linux and macOS even
while Windows is the first developer workflow.

## Platform Abstraction

Sloppy-owned APIs that isolate OS behavior from core runtime modules.

## Platform Backend

A platform-specific implementation under `src/platform/*`, such as `win32`, `posix`,
`linux`, or `macos`.

## OS API Leakage

Direct OS headers, calls, or assumptions appearing in core runtime modules instead of a
platform backend.

## Core Runtime Module

Portable runtime code outside platform implementation directories.

## Platform Implementation Directory

A directory under `src/platform/` where OS-specific headers and calls are allowed according
to the platform boundary rules.
