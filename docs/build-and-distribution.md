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
- add runtime dependencies before their documented phase;
- add Linux/macOS presets that must pass today.

## Current Phase

The project builds a placeholder `sloppy` executable and a placeholder Rust `sloppyc`
binary. The default CMake project uses C only. V8 is not required.

TASK 07.A adds optional V8 SDK detection. The default build keeps the V8 bridge disabled.
TASK 07.C compiles the V8 bridge and V8-gated smoke test only when V8 is explicitly enabled
and the SDK gate passes. TASK 10.B adds required vcpkg manifest dependencies for yyjson,
llhttp, and libuv in the normal non-V8 build.

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
through the manifest for Plan JSON parsing. TASK 10.B introduces `llhttp` for HTTP/1
request-head parsing and `libuv` for a dependency/link smoke ahead of the future event-loop
backend.

Do not add sqlite, libpq, ODBC, or other runtime dependencies until the relevant
implementation phase. Do not add a second HTTP parser or a custom HTTP parser.

## V8 SDK Policy

V8 is special and not managed by vcpkg initially.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`.
- `SLOPPY_ENGINE` defaults to `none`; `SLOPPY_ENGINE=v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` is required only when V8 is enabled.

When V8 is disabled, configure prints `V8 bridge: disabled` and normal configure/build/test
gates continue without a V8 SDK. CI uses this default non-V8 path.

When V8 is enabled and `SLOPPY_V8_ROOT` is empty or invalid, CMake configure fails before
any bridge code is compiled. When the SDK is valid, CMake compiles
`src/engine/v8/engine_v8.cc`, links it only to the V8-enabled core target, and registers the
`engine.v8.smoke` and `execution.handwritten_artifact` tests.

Contributor path:

- use verified prebuilt V8 SDK artifacts;
- fetch through `tools/windows/fetch-v8.ps1` later;
- validate an existing SDK root with
  `.\tools\windows\fetch-v8.ps1 -ValidateOnly -V8Root <sdk-root>`;
- set `SLOPPY_V8_ROOT` to SDK root;
- configure explicitly with `-DSLOPPY_ENABLE_V8=ON -DSLOPPY_V8_ROOT=<sdk-root>` or
  `-DSLOPPY_ENGINE=v8 -DSLOPPY_V8_ROOT=<sdk-root>`;
- do not build V8 locally by default.

Maintainer path:

- build from official V8 source through `tools/windows/build-v8.ps1`;
- use depot_tools/GN/Ninja with `DEPOT_TOOLS_WIN_TOOLCHAIN=0` for the local Visual Studio
  toolchain;
- auto-detect the newest installed Windows SDK by default, or pass `-WindowsSdkVersion`
  when a specific SDK must be used;
- package only the Sloppy-compatible SDK surface;
- keep depot_tools, source trees, build trees, headers, libraries, and generated outputs
  under ignored local paths;
- publish checksums later when a prebuilt distribution channel exists.

Current SDK layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  lib/libc++*.lib
  support/libcxx/include/
  support/libcxx/buildtools/__config_site
  bin/  # optional runtime DLLs for dynamic SDKs
  share/sloppy-v8-sdk.json
```

The current source SDK is a monolithic release build and should be consumed through the
`windows-relwithdebinfo` preset. The default `windows-dev` Debug preset remains the normal
non-V8 contributor path.

The CMake gate validates both SDK layout and `share/sloppy-v8-sdk.json` before creating
`Sloppy::V8` as an imported interface target. The manifest must match the pinned V8
revision and ABI flags that CMake applies to the bridge compile. V8 headers/types remain
isolated to `src/engine/v8/`.

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
    bootstrap/
      sloppy/
  include/
    sloppy/
  share/sloppy/
    licenses/
    schemas/
    docs/
```

The exact layout may change before release packaging, but binaries and support data should
be separated cleanly.

TASK 11.A starts the bootstrap support-data layout. Source assets live in
`stdlib/sloppy/`, the normal build copies them to
`<build>/lib/sloppy/bootstrap/sloppy/`, and CMake install places them under
`<prefix>/lib/sloppy/bootstrap/sloppy/`. The copy is plain file staging: no Node, npm,
bundler, transpiler, or package-manager metadata is involved.

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
- missing or invalid V8 SDK when V8 is explicitly enabled;
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

- Exact V8 SDK update cadence.
- Exact verified prebuilt V8 SDK source and checksum format.
- Whether release ZIP includes debug symbols separately.
- Whether `sloppyc` is installed by CMake or packaged by Cargo first.
- Exact Linux/macOS preset timing.
