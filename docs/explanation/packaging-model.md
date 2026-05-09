# Build And Distribution

## Purpose

This document explains how Sloppy builds and packages local development
artifacts today.

## Build Model

`tools/windows/dev.ps1` is the canonical Windows wrapper. It separates commands
by task:

- `doctor` checks dependency and tooling setup;
- `configure`, `build`, and `test` drive the local build;
- `lint` and `format-check` enforce code/doc standards;
- `package` and `test-package` create and check local archives;
- V8 mode is explicit (`OFF`, `AUTO`, `REQUIRED`) and independent from default
  non-V8 builds.

This keeps default build success separate from V8 execution, live providers, and
package behavior.

## Distribution Model

`tools/windows/package.ps1` creates an experimental local package with a dry-run
manifest contract.

Current packaging behavior is explicit:

- package text marks artifacts as experimental development builds;
- runtime/compiler binaries and source-controlled assets are bundled;
- V8 SDK headers/import libraries are excluded;
- optional V8 runtime binaries are included only by explicit opt-in;
- checksum and manifest files are produced for artifact verification.

## Current Boundaries

Different checks answer different questions:

- default build/test success is separate from V8 runtime execution;
- package creation is separate from public distribution work;
- presence of optional runtime files is separate from feature conformance;
- npm dry-run packaging installs the runtime, not app-level npm dependencies.

## Current Scope

Current build/distribution work is focused on internal packaging and validation.
Release publishing hardening and JavaScript-runtime compatibility are separate work.
