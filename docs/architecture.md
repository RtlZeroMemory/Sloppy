# Architecture

## Purpose

Sloppy is a pre-alpha TypeScript backend application runtime. The runtime kernel is written
in C, V8 integration is isolated behind a C++ bridge, and the `sloppyc` compiler/build tool
is written in Rust.

This document describes durable system boundaries and invariants. It is not a release note,
issue map, or public alpha claim.

## Current Status

Implemented foundations include:

- core C primitives for status, diagnostics, borrowed views, arenas, builders, interned
  metadata, resource IDs, request scopes, and app lifecycle cleanup;
- Plan parsing and validation for the current alpha schema;
- deterministic compiler output for the supported JavaScript source subset;
- source-input and artifact-based development execution paths;
- bounded HTTP parser, route matching, dispatch, response writer, backend lifecycle, and
  localhost transport;
- sequential HTTP/1.1 keep-alive and scoped chunked transport behavior for the supported
  native/runtime lanes;
- runtime feature descriptors and activation checks;
- provider metadata, native provider boundaries, provider executor infrastructure, and the
  executor-backed SQLite V8 bridge;
- optional V8 handler execution, request-context/result conversion, bounded Promise
  settlement, exception diagnostics, and source-map primary-span remapping;
- docs, tests, conformance fixtures, package fixtures, fuzz/property scaffolding, and
  benchmark harnesses as separate evidence lanes.

Default local and CI gates do not prove optional V8 behavior, live providers, public alpha
readiness, production HTTP behavior, package release readiness, or benchmark-backed
performance claims.

## Non-Claims

Sloppy does not currently claim:

- public alpha release status;
- production readiness;
- Node, Bun, Deno, npm, or package-manager compatibility;
- production HTTP edge behavior, graceful drain, HTTP/2, HTTP/3, WebSockets, or SSE;
- final Framework v2 API shape;
- broad JavaScript-to-native provider bridges beyond the current scoped SQLite bridge;
- OS sandboxing;
- plugin ABI stability;
- performance superiority.

## System Layers

```text
source app
  -> sloppyc
  -> Plan + bundle + source map
  -> app host
  -> runtime kernel
  -> engine bridge / providers / platform backends
```

`sloppyc` owns source parsing, supported syntax validation, deterministic artifact
emission, and Plan metadata generation. The C app host owns artifact loading, Plan
validation, runtime feature activation, lifecycle setup, and runtime dispatch. Engine,
provider, transport, and platform layers are implementation details behind Sloppy-owned
interfaces.

## Runtime Kernel

The C runtime owns:

- memory and ownership primitives;
- diagnostics and rendering helpers;
- Plan parsing and validated metadata views;
- route pattern parsing and matching;
- HTTP parser/backend/transport state;
- runtime feature and capability registries;
- provider executor and native provider contracts;
- app/request lifecycle cleanup;
- async completion ownership rules.

Core modules must remain independent from OS headers, V8 types, and JavaScript object
models. Platform and engine details enter only through documented Sloppy-owned boundaries.

## Engine Boundary

V8 is optional and isolated. Public headers and core modules must not expose V8 types,
handles, or ownership concepts. The C ABI passes plain Sloppy values and diagnostics; the
C++ bridge converts to and from V8 values internally.

V8 work follows these invariants:

- one owner thread enters each isolate/context;
- wrong-thread entry fails before touching V8 state;
- JS never receives raw native pointers;
- V8 handles never escape the bridge;
- native strings and result bytes are copied out before returning to C;
- Promise/microtask drains are bounded and owner-thread only;
- pending or rejected Promises produce deterministic failure instead of fake success.

## Platform Boundary

All OS and event-loop specifics live under platform modules. Core runtime code sees opaque
platform types and Sloppy-owned callbacks. libuv, sockets, timers, filesystem handles, and
process APIs must not leak into public headers or core modules.

## HTTP Boundary

The current HTTP path is a bounded localhost/runtime transport, not a production edge
server. The parser and backend own complete-buffer request semantics, request lifecycle,
body limits, diagnostics, cancellation, response serialization, and an optional inbound
OpenSSL TLS wrapper. The transport owns listener/connection handles, bounded connection
storage, sequential keep-alive, scoped chunked behavior, and TLS handshake/encrypted-write
plumbing where configured.

Deferred HTTP work includes production graceful drain, HTTP/2/3, WebSockets, SSE, static
files, public streaming response APIs, ALPN/mTLS/custom certificate validation, and
production-edge hardening.

## Provider Boundary

Provider metadata and capability checks are Plan-visible. Native provider work stays behind
provider contracts and executor boundaries. JavaScript receives capability-checked bridge
functions, not native pointers or provider-owned resources.

SQLite has the serialized executable bridge. PostgreSQL has a V8-gated true-async bridge
over nonblocking libpq plus bounded pooling. SQL Server provider metadata/native
boundaries exist as scoped foundations until the ODBC async bridge lane proves real async
driver behavior. Live-provider evidence remains separate from default evidence.

## Compiler Boundary

The compiler supports a deliberate subset. Unsupported syntax must fail with clear
diagnostics. Compiler output is deterministic and path-normalized where required by
fixtures. The compiler does not claim TypeScript lowering, Node resolution, npm packages,
general module graph support, or final Framework v2 behavior.

See `docs/compiler.md` and `docs/compiler-supported-syntax.md`.

## Documentation Boundary

Current architecture docs describe implemented behavior and intended invariants. Historical
planning, audits, issue choreography, and construction-era status live under
`docs/project/archive/` or GitHub issues. Current docs may name tracked work only when the
identifier is the actual roadmap label for a future track; they should not explain the
system as a sequence of old tasks.

## Near-Term Direction

The current pre-alpha direction is:

- HTTP server hardening;
- TLS as a separate transport track;
- Framework v2 shape and compiler/runtime alignment;
- packaging, build, and distribution;
- realistic dogfood applications and examples;
- public alpha readiness gates.

Those tracks must preserve the non-claims above until the relevant implementation and
evidence lanes land.
