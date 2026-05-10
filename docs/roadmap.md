# Roadmap

Sloppy is pre-alpha. This page separates what exists now from the directions
that are still being designed.

## Current foundation

Current Sloppy includes:

- compiler source input for the supported app subset;
- deterministic Plan-backed artifacts;
- V8-backed handler execution on V8-enabled builds and alpha packages;
- `sloppy create`, `build`, `run`, `routes`, `capabilities`, `doctor`,
  `audit`, `openapi`, and `package`;
- HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS ALPN plus h2c;
- first-party APIs for routing, results, services, config, logging,
  capabilities, data, filesystem, network, OS, time, crypto, codec, workers,
  schema, and testing;
- Windows x64 and Linux x64 npm platform packages;
- source/archive build paths for other platforms.

## Near-term alpha work

The next alpha work is about polish and coverage rather than a new runtime
shape:

- clearer diagnostics for unsupported source shapes;
- stronger docs around the compiler subset;
- more package/install coverage across Linux baselines;
- more examples that show real app structure without implying Node
  compatibility;
- broader provider evidence for PostgreSQL and SQL Server;
- repeated HTTP/2 conformance and client/server coverage.

## Program Mode

Sloppy should not be limited to web APIs. Program Mode is the planned path for
CLI tools, background services, workers, and terminal apps.

Expected metadata direction:

- entrypoint;
- lifecycle;
- declared capabilities;
- filesystem, network, process, and terminal needs;
- package/runtime metadata.

Program Mode is a roadmap direction, not current support.

## FFI and native modules

Native extension work is future work. Likely stages:

- Sloppy-owned native module ABI;
- controlled C ABI FFI;
- native dependency metadata in the Plan;
- possible N-API compatibility later if it fits the runtime boundary.

No current docs should imply arbitrary native modules work today.

## Dependency story

Sloppy apps do not resolve npm app dependencies today. The current path is the
first-party stdlib plus compiler-supported source. Pure ESM dependency bundling
may be explored later. Node built-ins are supported only when Sloppy implements
that API explicitly.

## Production hardening

Production hardening needs more work:

- graceful shutdown and connection drain;
- broader TLS policy;
- observability surface;
- platform coverage;
- conformance suites;
- reproducible performance reports.
