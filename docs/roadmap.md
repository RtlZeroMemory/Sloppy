# Roadmap

Sloppy is public alpha software. This page separates what
exists now from the directions that are still being designed.

## Current Runtime

Current Sloppy includes:

- compiler source input for the supported app subset;
- deterministic Plan-backed artifacts;
- V8-backed handler execution on V8-enabled builds and alpha packages;
- Program Mode for route-free console tools, local automation, packaged
  programs, and stdlib-backed worker entrypoints;
- `sloppy create`, `build`, `run`, `routes`, `deps`, `capabilities`,
  `doctor`, `audit`, `openapi`, and `package`;
- HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS ALPN plus h2c;
- native Core streams for bounded memory/chunk-list flow, partial-write pump
  retry, HTTP response stream serialization, transport stream descriptors,
  request-body readable adapters, benchmarks, and fuzzing;
- experimental build-time static file routes for supported project assets;
- first-party APIs for routing, results, services, config, logging,
  capabilities, health, metrics, management, data, filesystem, network, OS,
  time, crypto, codec, workers, schema, and testing;
- experimental typed, Plan-visible FFI for deliberate C ABI interop;
- Windows x64, Linux x64 glibc, and macOS npm platform packages;
- source/archive build paths for Linux arm64, Windows arm64, and other
  platforms without an alpha package.

## Near-term alpha work

The next alpha work is about polish and coverage rather than a new runtime
shape:

- clearer diagnostics for unsupported source shapes;
- stronger docs around the compiler subset;
- more package/install coverage across Linux baselines;
- more examples that show real app structure without implying Node
  compatibility;
- broader provider evidence for PostgreSQL and SQL Server;
- repeated HTTP/2 conformance and client/server coverage;
- incremental adoption of Core streams inside filesystem, process, codec, and
  HTTP client internals where that reduces duplicated bounded chunk handling;
- stronger production static-file behavior such as range requests,
  compression negotiation, and cache policy hardening.

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
- Plan inspection through `routes`, `deps`, `capabilities`, `doctor`, and
  `audit`.

Future Program Mode work is not hidden Node emulation. It includes more
job/lifecycle policy, richer worker packaging, raw terminal ergonomics if they
fit the Sloppy boundary, and broader conformance evidence.

## FFI and native modules

Typed C ABI FFI exists today as an experimental, unsafe boundary. It supports
static top-level library/function declarations, typed marshaling, refs, buffers,
pointer-based sequential structs, package metadata for mapped local libraries,
and V8/libffi runtime execution when the needed native library is available.

What remains future work:

- Sloppy-owned native module ABI;
- possible N-API compatibility later if it fits the runtime boundary.

No current docs should imply arbitrary native modules, callbacks, C++ ABI,
native addons, or N-API work today.

## Dependency story

Sloppy now has an experimental installed-package graph for compatible
pure-JavaScript dependencies. The current path is first-party stdlib plus
compiler-supported source, bundled package modules, and explicit partial
`node:*` shims. Registry install, semver solving, native addons, and full Node
compatibility remain separate roadmap work. See
[Node compatibility roadmap](roadmap/node-compatibility.md).

## Production hardening

Production hardening needs more work before Sloppy can be described as
production-ready:

- graceful shutdown and connection drain;
- broader TLS policy;
- deeper native transport observability and production-grade operations evidence;
- platform coverage;
- conformance suites;
- reproducible performance reports.
