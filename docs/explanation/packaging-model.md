# Build And Distribution

## Purpose

This document explains the difference between building from source, creating a
package archive, and using a future launcher-style install.

## Build Model

Source builds are for contributors working inside the repository.
`tools/windows/dev.ps1` is the canonical Windows wrapper. It separates commands
by task:

- `doctor` checks dependency and tooling setup;
- `configure`, `build`, and `test` drive the local build;
- `lint` and `format-check` enforce code/doc standards;
- `package` and `test-package` create and check local archives;
- V8 mode is explicit (`OFF`, `AUTO`, `REQUIRED`) and independent from default
  non-V8 builds.

This lets contributors build and test the repository without also requiring V8
execution, live databases, or package archive checks every time.

## Distribution Model

Package archives are for trying Sloppy outside the source checkout. They are
experimental local development artifacts today, not a public release channel.

Current packaging behavior is explicit:

- package text marks artifacts as experimental development builds;
- runtime/compiler binaries and source-controlled assets are bundled;
- V8 SDK headers/import libraries are excluded;
- optional V8 runtime binaries are included only by explicit opt-in;
- checksum and manifest files are produced for artifact verification.

## npm Launcher Model

An npm launcher can be useful as an install path for the Sloppy CLI. That is
separate from application dependency support. Installing Sloppy through npm
would install the tool; it would not make Sloppy apps compatible with
`node_modules`.

## Current Boundaries

Different setups answer different questions:

- default build/test success is separate from V8 runtime execution;
- package creation is separate from public distribution work;
- presence of optional runtime files is separate from feature conformance;
- npm dry-run packaging installs the runtime, not app-level npm dependencies.

## Current Scope

Current build/distribution work is focused on local package archives and
development workflows. Release publishing hardening and JavaScript-runtime
compatibility are future work.
