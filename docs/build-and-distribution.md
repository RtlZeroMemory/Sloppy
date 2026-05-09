# Build And Distribution

## Purpose

This document explains what Sloppy build/distribution lanes currently mean, and
what they do not mean.

## Build Model

`tools/windows/dev.ps1` is the canonical Windows wrapper. It separates lanes by
intent:

- `doctor` checks dependency/tooling readiness;
- `configure`, `build`, and `test` drive the local build lane;
- `lint` and `format-check` enforce code/doc standards;
- `package` and `test-package` are separate artifact lanes;
- V8 mode is explicit (`OFF`, `AUTO`, `REQUIRED`) and independent from default
  non-V8 builds.

This prevents default build success from being misread as V8, provider, or
release evidence.

## Distribution Model

`tools/windows/package.ps1` creates an experimental local package with a dry-run
manifest contract.

Current packaging behavior is explicit:

- package text marks artifacts as experimental and not public release;
- runtime/compiler binaries and source-controlled assets are bundled;
- V8 SDK headers/import libraries are excluded;
- optional V8 runtime binaries are included only by explicit opt-in;
- checksum and manifest files are produced for artifact verification.

## Evidence Boundaries

Build/distribution evidence must stay lane-specific:

- default build/test success does not imply V8 runtime execution;
- package creation does not imply public release readiness;
- presence of optional runtime files does not imply feature conformance;
- npm dry-run packaging does not imply npm app dependency compatibility.

## Non-Claims

Current build/distribution work does not claim public release readiness,
production hardening, or Node/Bun/Deno/package-manager compatibility.
