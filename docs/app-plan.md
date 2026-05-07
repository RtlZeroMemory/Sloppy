# Application Plan

The Sloppy Plan is the compiler/runtime contract. It is deterministic metadata consumed by
native validation, CLI inspection, runtime feature activation, and V8 artifact execution.

## Purpose

The Plan lets Sloppy validate application shape before runtime execution:

- route metadata and handler IDs;
- generated artifact names and hashes;
- required runtime features;
- provider and capability metadata;
- compiler/source metadata for diagnostics;
- configuration metadata where the compiler emits it.

The Plan is not a package manifest, npm metadata format, Node runtime layer, or public
extension system.

## Current Runtime Contract

Plan v1 alpha remains the current runtime contract. Supported artifacts contain
`app.plan.json`, generated `app.js`, and source-map metadata where available. The runtime
validates schema version, route/handler/provider/capability structure, artifact hashes,
runtime target support, and required features before entering V8.

Handler IDs are compiler-owned numeric IDs. Current executable V8 dispatch uses generated
artifact registration and registered handler dispatch; direct numeric handler-call ABI
entry remains unsupported for the noop engine.

## Routes And Handlers

The compiler can emit route metadata for the supported source subset:

- literal route patterns;
- supported HTTP methods;
- optional route names;
- source metadata;
- direct handler IDs;
- simple request context usage in supported handlers.

The native runtime validates route metadata, builds a deterministic route table, and
dispatches supported requests through V8 in the V8 lane. Dynamic route registration,
middleware, modules, automatic validation, arbitrary imports, and full TypeScript semantics
remain outside the current source subset.

## Server Config Metadata

The runtime consumes Slop-owned server metadata emitted by the current compiler/config
pipeline for `sloppy run`: host, port, max connections, max request body bytes, request
timeout, keep-alive enablement, keep-alive idle timeout, and max requests per connection.
Malformed, zero, unsupported, or range-overflowing values fail closed before serving work.
Route-level limits and trusted proxy policy are not Plan metadata yet.

## Required Features

`requiredFeatures[]` records runtime features that must be active before execution. The
runtime feature registry rejects missing features before engine initialization. Current
feature families include the bootstrap stdlib, filesystem, time, crypto, codec, network,
OS/process, workers, HTTP transport, and provider-related features where implemented.

Feature descriptors are evidence boundaries. A Plan feature entry does not prove that every
possible API surface is implemented; it only declares the feature required by the compiled
artifact and validated by the runtime.

## Providers And Capabilities

Plan provider and capability metadata supports policy checks and doctor/audit visibility.
SQLite provider/capability metadata is consumed by the V8 SQLite bridge in scoped lanes.
PostgreSQL, SQL Server, filesystem, and network capability metadata must not be presented
as complete JavaScript bridge or OS-sandbox evidence unless a scoped implementation and
evidence lane prove it.

Credential-bearing fields must not be persisted in Plan metadata. Use redacted placeholders,
config references, or secret-source references.

## Source Input

`sloppy run <source.js>` invokes `sloppyc build`, writes generated artifacts, validates
them through the same Plan/artifact path, and then runs artifacts. It is a development
shortcut over the artifact path, not a watch mode, cache policy, package manager, or full
TypeScript build system.

## Non-Goals

- Node/Bun/Deno/npm behavior.
- Package-manager metadata.
- Arbitrary import graph resolution.
- Public plugin extension points.
- Public alpha or production-readiness claims.
- Performance claims from Plan validation or artifact smoke tests.

## Source Docs

- `docs/compiler.md`
- `docs/compiler-supported-syntax.md`
- `docs/execution-model.md`
- `docs/security-permissions.md`
- `docs/project/core-api-platform-map.md`
