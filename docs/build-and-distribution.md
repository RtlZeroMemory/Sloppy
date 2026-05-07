# Build And Distribution

## Purpose

This document describes how Sloppy builds local development artifacts today and what it
does not yet claim for packaging or release.

## Current Build Outputs

The repository builds:

- the native `sloppy` runtime CLI;
- the Rust `sloppyc` compiler CLI;
- optional V8-backed runtime code when the V8 SDK gate is enabled;
- test, conformance, and benchmark harness binaries where configured;
- experimental package layouts and package fixtures.

The default build keeps optional V8 behavior gated. A default build/test pass is not V8,
package release, live-provider, production HTTP, or benchmark evidence.

## Windows Workflow

Canonical local workflow:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 lint
```

V8-enabled work must run the resolver and V8 preset when the task touches runtime,
app-host, compiler, bootstrap, provider, configuration, or V8-adjacent behavior and the
local resolver succeeds.

## Dependencies

C/C++ dependencies are managed through the repository's CMake/vcpkg workflow. V8 SDK
resolution is explicit and optional. Rust dependencies are owned by the `compiler/`
project. JavaScript dependencies are not a package-manager surface for Sloppy apps.

## Packages

Current package artifacts are experimental evidence for layout and outside-checkout
behavior. They are not a public release, installer, hosted distribution, or compatibility
claim. Package fixtures must not recompile source when the package contract says execution
comes from packaged artifacts.

## CI

CI separates default non-V8, static, Rust, package, optional V8, and heavier lanes. Skipped
or unavailable optional lanes are not pass evidence. Required checks must be green and
current before merge.

## Non-Claims

Sloppy does not currently claim production-ready builds, public alpha release artifacts,
TLS distribution, package-manager integration, Node/Bun/Deno compatibility, or benchmarked
performance.
