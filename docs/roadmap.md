# Roadmap

Sloppy is pre-alpha. This page separates what exists now from the directions
that are still being designed.

## Current foundation

Current Sloppy includes:

- compiler source input for the supported app subset;
- deterministic Plan-backed artifacts;
- V8-backed handler execution on V8-enabled builds and alpha packages;
- Program Mode for route-free console tools, local automation, packaged
  programs, and stdlib-backed worker entrypoints;
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

Sloppy is not limited to web APIs. Program Mode is the current route-free path
for CLI tools, local automation, background jobs, and worker entrypoints.

Current support:

- `sloppy run src/main.ts`, `sloppy build src/main.ts`,
  `sloppy package src/main.ts`, and project mode with `kind: "program"`;
- named `main(args, ctx)`, default function entrypoints, and top-level-only
  modules;
- arguments after `--`, Program context metadata, console stdout/stderr, and
  numeric exit codes;
- Sloppy stdlib imports for filesystem, network, OS/process, time, crypto,
  codec, and workers where the runtime bridge is available;
- package manifests that record `kind: "program"` and copied artifact paths;
- Plan inspection through `routes`, `capabilities`, `doctor`, and `audit`.

Future Program Mode work is not hidden Node emulation. It includes more
job/lifecycle policy, richer worker packaging, raw terminal ergonomics if they
fit the Sloppy boundary, and broader conformance evidence.

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
