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
- `sloppy create`, `build`, `run`, `routes`, `deps`, `capabilities`,
  `doctor`, `audit`, `openapi`, and `package`;
- HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS ALPN plus h2c;
- first-party APIs for routing, results, services, config, logging,
  capabilities, data, filesystem, network, OS, time, crypto, codec, workers,
  schema, and testing;
- experimental installed package graph for compatible pure-JavaScript
  dependencies, with bundled module artifacts and `sloppy deps` inspection;
- experimental, unsafe `sloppy/ffi` foundation for typed C ABI calls with
  Plan-visible metadata and packaged local native libraries;
- published Windows x64, Linux x64, and macOS npm platform packages;
- source/archive build paths for arm64 and other platforms.

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
- Plan inspection through `routes`, `deps`, `capabilities`, `doctor`, and
  `audit`.

Future Program Mode work is not hidden Node emulation. It includes more
job/lifecycle policy, richer worker packaging, raw terminal ergonomics if they
fit the Sloppy boundary, and broader conformance evidence.

## FFI and native modules

`sloppy/ffi` is now an experimental, unsafe foundation: typed C ABI calls,
refs and buffers, pointer-based sequential structs, Plan-visible declarations,
and packaged local native libraries. Wrong signatures or invalid pointers can
crash the process. See [Native FFI](reference/ffi.md).

Future native interop work includes:

- callbacks from native code into JavaScript;
- variadic calls;
- struct-by-value parameter and return shapes;
- async or off-thread FFI invocation;
- a Sloppy-owned native module ABI separate from the FFI foundation;
- possible N-API compatibility if it fits the runtime boundary.

No current docs should imply Node native addons or N-API work today.

## Dependency story

Sloppy now has an experimental installed-package graph for compatible
pure-JavaScript dependencies. The current path is first-party stdlib plus
compiler-supported source, bundled package modules, and explicit partial
`node:*` shims. Registry install, semver solving, native addons, and full Node
compatibility remain separate roadmap work. See
[Node compatibility roadmap](roadmap/node-compatibility.md).

## Platform and package distribution

- Windows x64, Linux x64, and macOS are the published npm platform packages.
- arm64 packaging is being validated through the alpha release flow.
- Source/archive builds remain the path for unsupported platforms.

## Production hardening

Production hardening needs more work:

- graceful shutdown and connection drain;
- broader TLS policy;
- observability surface;
- broader platform coverage;
- conformance suites;
- reproducible performance reports.

## Tests, fuzzing, and benchmarks

- More fuzzing across compiler input, Plan parsing, HTTP transport, and
  package resolution.
- Repeatable benchmark methodology before any competitive numbers ship; the
  current realistic suite is local engineering evidence only. See
  [Performance](about/performance.md).
