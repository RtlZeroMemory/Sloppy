# Build And Distribution

## Purpose

This document defines Sloppy's build, dependency, tooling, packaging, and artifact hygiene
strategy.

## Scope

It covers:

- Windows-first, cross-platform tooling layout;
- CMake presets;
- vcpkg policy;
- V8 SDK policy;
- prebuilt V8 contributor path;
- source-built V8 maintainer path;
- PowerShell tooling under `tools/windows`;
- future Unix tooling under `tools/unix`;
- release ZIP and install layout;
- source archive hygiene;
- implementation tasks and acceptance criteria.

## Non-Goals

The foundation phase does not:

- build V8;
- fetch V8;
- package releases;
- add runtime dependencies;
- add Linux/macOS presets that must pass today.

## Current Phase

The project builds a placeholder `sloppy` executable and a placeholder Rust `sloppyc`
binary. The default CMake project uses C only. V8 is not required.

## Future Phase

Future phases add V8 SDK discovery, runtime dependencies, install layout, release staging,
and platform-specific presets for Linux/macOS.

## Public API Shape

Supported build commands today:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

## Windows-First, Cross-Platform By Design

Windows x64 is the first developer and distribution target. Linux and macOS are
architectural targets from day one.

Core build targets must avoid Windows-only assumptions. Platform-specific implementation
files should be selected explicitly when they exist.

## CMake Presets

Current presets:

- `windows-dev`;
- `windows-release`;
- `windows-relwithdebinfo`;
- `windows-asan`.

Future presets:

- `linux-dev`;
- `linux-release`;
- `linux-asan`;
- `macos-dev`;
- `macos-release`.

Future presets should not be added as fake green jobs. They should become active when the
platform implementation layer can support them.

## vcpkg Policy

vcpkg manifest mode is reserved for normal C dependencies. TASK 06.B introduces `yyjson`
through the manifest for Plan JSON parsing.

Do not add libuv, llhttp, sqlite, libpq, ODBC, or other runtime dependencies until the
relevant implementation phase.

## V8 SDK Policy

V8 is special and not managed by vcpkg initially.

Contributor path:

- use verified prebuilt V8 SDK artifacts;
- fetch through `tools/windows/fetch-v8.ps1` later;
- set `SLOPPY_V8_ROOT` to SDK root;
- do not build V8 locally by default.

Maintainer path:

- build from source through future `tools/windows/build-v8.ps1`;
- use depot_tools/GN/Ninja;
- package a Sloppy-compatible SDK;
- publish checksums and manifest metadata.

Expected future SDK layout:

```text
.sdeps/v8/
  include/
  lib/
  bin/
  share/sloppy-v8-sdk.json
```

## Tool Layout

```text
tools/
  windows/
    bootstrap.ps1
    dev.ps1
    fetch-v8.ps1
    build-v8.ps1
    package.ps1
    check-platform-boundaries.ps1
  unix/
    README.md
```

Root `tools/*.ps1` scripts are compatibility forwarders. Future Bash scripts belong under
`tools/unix/`.

## Internal Architecture

CMake owns the C runtime build. Cargo owns `sloppyc`. PowerShell scripts orchestrate the
Windows developer loop. vcpkg owns ordinary C dependencies only when they are introduced.
V8 remains a special SDK dependency.

## Lifecycle Flow

Local development flow:

1. initialize Visual Studio developer shell;
2. run bootstrap;
3. configure CMake preset;
4. build C runtime and Rust compiler placeholder;
5. run CTest and cargo tests;
6. run format/lint gates;
7. keep generated artifacts ignored.

Future release flow:

1. clean checkout;
2. configure release preset;
3. build release artifacts;
4. stage install layout;
5. collect licenses and manifests;
6. create ZIP;
7. verify ZIP in a clean directory.

## Install Layout

Future install layout:

```text
prefix/
  bin/
    sloppy.exe
    sloppyc.exe
  lib/sloppy/
    engines/
    stdlib/
  include/
    sloppy/
  share/sloppy/
    licenses/
    schemas/
    docs/
```

The exact layout may change before release packaging, but binaries and support data should
be separated cleanly.

## Release ZIP Layout

First distribution target:

```text
sloppy-x.y.z-windows-x64/
  bin/sloppy.exe
  bin/sloppyc.exe
  lib/sloppy/
  share/sloppy/licenses/
  README.md
```

Scoop and winget come after the ZIP contract is stable. Homebrew comes after Linux/macOS
support is real.

## Source Archive Hygiene

Source archives and review ZIPs must exclude:

- `.git/`;
- `build/`;
- `compiler/target/`;
- `target/`;
- `.sdeps/`;
- `.sloppy/`;
- V8 SDKs;
- local binaries;
- release archives.

Generated artifacts must be reproducible from source, tools, and documented SDKs.

Clean review archive policy:

1. start from a clean `git status --short --ignored` review;
2. include source, docs, scripts, configs, and tests only;
3. exclude VCS internals unless explicitly requested;
4. exclude local build outputs and dependency caches;
5. exclude local binaries, PDBs, archives, V8 SDKs, and `.sloppy/`;
6. prefer `git archive` or an equivalent tracked-file-only process once the repo has
   commits.

Do not upload `.git/`, `build/`, `compiler/target/`, `.sdeps/`, `.sloppy/`, or local
binaries in review zips.

Use the tracked-file review helper when available:

```powershell
.\tools\windows\create-review-zip.ps1
```

Equivalent committed-source command:

```powershell
git archive --format=zip --output Slop-review.zip HEAD
```

## Error And Diagnostic Behavior

Build scripts should fail with clear messages:

- missing tool;
- missing MSVC/Windows SDK environment;
- missing V8 SDK when V8 becomes required;
- unsupported preset;
- generated artifact accidentally tracked.

## Testing Requirements

Build/tooling tests currently include:

- bootstrap smoke;
- configure/build/test;
- format-check;
- lint;
- artifact hygiene;
- platform-boundary scanner.

Future packaging tests should unpack a release ZIP and run `sloppy --version` and
`sloppyc --version`.

## Implementation Tasks

- Keep root wrappers forwarding to `tools/windows`.
- Add Unix scripts only when Linux/macOS build path exists.
- Add V8 SDK manifest format.
- Add package staging directory.
- Add release ZIP script.
- Add install tests.
- Add packaging CI job after binaries become real.
- Add clean review archive helper later if repeated manual packaging becomes error-prone.
- Add Linux/macOS presets only when platform code and CI can honestly support them.

## Acceptance Criteria

Build/distribution foundation is accepted when:

- Windows preset configures/builds/tests;
- V8 is optional for foundation build;
- dependency manifest has no premature runtime dependencies;
- tool layout separates Windows and future Unix scripts;
- generated artifacts are ignored and untracked;
- packaging policy excludes VCS internals and local build output.
- docs explain root wrapper and `tools/windows` policy consistently;
- review/source archive hygiene is documented before release packaging exists.

## Open Questions

- Exact V8 SDK version pinning policy.
- Whether release ZIP includes debug symbols separately.
- Whether `sloppyc` is installed by CMake or packaged by Cargo first.
- Exact Linux/macOS preset timing.
