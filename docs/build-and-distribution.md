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

The packaging foundation does not:

- build V8;
- fetch V8;
- publish public releases;
- sign or notarize artifacts;
- build installers;
- add Homebrew, Scoop, WinGet, MSI/MSIX, or other package-manager behavior;
- add auto-update behavior;
- add runtime dependencies before their documented phase;
- make V8 or live database services mandatory in default CI.

## Current Phase

The project builds the `sloppy` runtime CLI and Rust `sloppyc` compiler CLI. EPIC-21 added
the first compiler extraction path, ENGINE-02 expands it with supported method/async/
provider/capability/source-map metadata, EPIC-22 added the dev-only `sloppy run --artifacts` path for
V8-enabled builds, EPIC-23 added the request/response boundary, and EPIC-24 loads the
classic bootstrap runtime asset plus generated handler registrations in that V8-gated
path. V8 is still not required for default builds.
MAIN1-05 hardens that optional V8 path with owner-thread checks, per-engine lifecycle
diagnostics, explicit Promise rejection, and generated-source exception locations.
ENGINE-03 adds V8-gated microtask-only Promise settlement for direct async handlers. These
cuts do not make V8 required for default builds and do not add Node/npm, timers, fetch, fs,
native async provider queues, or runtime source-map remapping.

EPIC-20 also builds `sloppy_bench`, a native benchmark executable for manual performance
validation. It is not installed or packaged as a user-facing CLI surface.

TASK 07.A adds optional V8 SDK detection. The default build keeps the V8 bridge disabled.
TASK 07.C compiles the V8 bridge and V8-gated smoke test only when V8 is explicitly enabled
and the SDK gate passes. TASK 10.B adds required vcpkg manifest dependencies for yyjson,
llhttp, and libuv in the normal non-V8 build.

EPIC-25 adds the first local package layout and smoke tooling. These packages are
experimental development artifacts, not public release artifacts. They prove that the
current runtime CLI, compiler CLI, bootstrap stdlib assets, manifest, and checksum can be
staged into one archive and smoke-tested outside the checkout.

EPIC-26 adds hosted default non-V8 CI gates for Windows clang-cl, Linux clang, Linux gcc,
and macOS clang. It also adds a manual optional V8 workflow path and explicit provider
gate reporting. MAIN1-12 keeps that evidence split and hardens package-smoke policy.
Default CI still does not prove V8 execution, live PostgreSQL, live SQL Server, package
runtime readiness, or package-manager distribution.

## Future Phase

Future phases add public release hardening, signed/notarized artifacts, installers,
required package smoke in hosted CI, and package-manager integrations after the local
package contract is stable.

## Public API Shape

Supported build commands today:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
```

Packaging commands:

```powershell
.\tools\windows\package.ps1 -Configuration Release
.\tools\windows\package.ps1 -Configuration Release -Smoke
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
```

The root `tools\package.ps1` wrapper forwards to `tools\windows\package.ps1`.

Unix package commands:

```sh
tools/unix/package.sh --configuration Release
tools/unix/test-package.sh --package-path artifacts/packages/sloppy-0.0.0-dev-<platform>-<arch>.tar.gz
```

The Unix smoke command is local package-layout validation. It remains local/manual until a
scoped Linux/macOS package-smoke job runs in hosted CI.

Benchmark wrapper:

```powershell
.\tools\windows\bench.ps1 -List
.\tools\windows\bench.ps1 -Smoke -Json
.\tools\windows\bench.ps1 -Configuration Release
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
- `windows-asan`;
- `linux-clang`;
- `linux-gcc`;
- `macos-clang`.

The Linux/macOS presets are default non-V8 CI presets. They disable SQL Server ODBC by
default because the current SQL Server provider is Windows-first for live ODBC execution
and non-Windows jobs should cover unavailable/stub behavior rather than require a driver.
They are not release/package presets and should not be used to claim V8 or live database
coverage.

## vcpkg Policy

vcpkg manifest mode is reserved for normal C dependencies. TASK 06.B introduces `yyjson`
through the manifest for Plan JSON parsing. TASK 10.B introduces `llhttp` for HTTP/1
request-head parsing and `libuv` for a dependency/link smoke ahead of the future event-loop
backend. EPIC-16 introduces `sqlite3` for the native SQLite provider. EPIC-17 introduces
`libpq` for the native PostgreSQL provider. EPIC-18 introduces ODBC discovery for the
native SQL Server provider through the platform/toolchain rather than vcpkg driver
packages.

Do not add additional runtime dependencies before their relevant implementation phase. Do
not add a second HTTP parser or a custom HTTP parser.

PostgreSQL build and distribution notes:

- libpq is restored through the vcpkg manifest and linked through CMake's
  `PostgreSQL::PostgreSQL` target.
- Default tests do not require a running PostgreSQL server.
- Live PostgreSQL tests run only when `SLOPPY_POSTGRES_TEST_URL` is set.
- The separate live CTest target is reported as skipped when that variable is unset; a
  skipped live target is not evidence that live PostgreSQL passed.
- Release packaging still needs an explicit libpq DLL/shared-library copy and license
  strategy before packaged PostgreSQL runtime support can be claimed; do not assume a
  system-global PostgreSQL installation will be present.

SQL Server build and distribution notes:

- `SLOPPY_ENABLE_SQLSERVER` defaults to `ON` on Windows and `OFF` elsewhere.
- When enabled, CMake uses `find_package(ODBC REQUIRED)` and links `ODBC::ODBC`.
- Sloppy does not install Microsoft ODBC Driver for SQL Server, require admin, vendor
  driver binaries, or download drivers.
- Default tests do not require a running SQL Server or installed SQL Server ODBC driver.
- Live SQL Server tests run only when `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` is set.
- The separate live CTest target is reported as skipped when that variable is unset; a
  skipped live target is not evidence that live SQL Server passed.
- Release packaging must document the external Microsoft ODBC Driver prerequisite and any
  future non-Windows unixODBC/iODBC strategy before claiming packaged SQL Server support.

## V8 SDK Policy

V8 is special and not managed by vcpkg initially.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`.
- `SLOPPY_ENGINE` defaults to `none`; `SLOPPY_ENGINE=v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` is an explicit SDK-root override for V8-enabled builds.
- `SLOPPY_V8_SDK_HINTS` is an optional path-list of portable SDK/cache roots for local
  discovery.

When V8 is disabled, configure prints `V8 bridge: disabled` and normal configure/build/test
gates continue without a V8 SDK. Required CI uses this default non-V8 path.

When V8 is enabled through the Windows wrapper, `tools/windows/v8-sdk.ps1` resolves and
validates the SDK from `-V8Root`, `SLOPPY_V8_ROOT`, `SLOPPY_V8_SDK_HINTS`, this worktree's
`.sdeps/v8/windows-x64`, or the same path in registered git worktrees. The wrapper passes
the resolved SDK root into CMake as `SLOPPY_V8_ROOT`. Direct CMake users must pass
`-DSLOPPY_V8_ROOT=<sdk-root>` themselves. When the SDK root is empty or invalid, CMake
configure fails before any bridge code is compiled. When the SDK is valid, CMake compiles
the V8 engine core plus framework bridge and provider intrinsic modules under
`src/engine/v8/`, links them only to the V8-enabled core
target, and registers the `engine.v8.smoke`, `engine.v8.owner_thread`, and
`execution.handwritten_artifact` tests. Framework-specific bridge code must be added as
dedicated sibling modules. Provider bridges must be added as `intrinsics_<provider>.cc`
files registered through `intrinsics.cc`, not by expanding `engine_v8.cc`.

Contributor path:

- use verified prebuilt V8 SDK artifacts;
- fetch through `tools/windows/fetch-v8.ps1` later;
- discover and validate an existing SDK with `.\tools\windows\resolve-v8-sdk.ps1`;
- validate the resolved SDK layout with `.\tools\windows\fetch-v8.ps1 -ValidateOnly`;
- configure through the Windows wrapper with
  `.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8`;
- use direct CMake only from a shell that already has MSVC, the Windows SDK, and vcpkg
  configured and passes `-DSLOPPY_V8_ROOT=<sdk-root>`;
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

The Windows `dev.ps1` wrapper is the supported local configure path. It imports the Visual
Studio C++ environment, injects the vcpkg toolchain on fresh configure, and recreates a
preset build directory when a stale partial CMake cache was created without that toolchain.
Use `-FreshConfigure` when a preset should be deliberately rebuilt from scratch.

`tools/windows/v8-sdk.ps1` is the shared resolver/validator for V8 SDK layout and manifest
checks. `dev.ps1`, `fetch-v8.ps1`, `resolve-v8-sdk.ps1`, and V8-runtime packaging use that
helper so fresh worktrees and parallel Codex worktrees use the same discovery behavior.
New tooling must dot-source this helper rather than cloning local-path logic.

The CMake gate validates both SDK layout and `share/sloppy-v8-sdk.json` before creating
`Sloppy::V8` as an imported interface target. The manifest must match the pinned V8
revision and ABI flags that CMake applies to the bridge compile. V8 headers/types remain
isolated to `src/engine/v8/`.

Distribution policy:

- source builds use an ignored local V8 SDK under `.sdeps/v8/<platform-arch>` or an
  explicit `SLOPPY_V8_ROOT`;
- end-user packages should not require users to install the V8 SDK;
- release packages should eventually link V8 statically/monolithically when practical;
- bundled runtime DLL/shared libraries are the fallback for dynamic V8 builds;
- the repository and packages must not include V8 SDK headers, import libraries, source
  trees, or build outputs;
- `tools/windows/package.ps1` excludes `.sdeps/` and records `containsV8Sdk: false` in the
  package manifest;
- `-IncludeV8Runtime` may copy only dynamic runtime files from a V8 SDK `bin/` directory
  into `lib/sloppy/engines/v8/`; it must not copy SDK headers or import libraries;
- the default package is non-V8 unless the built `sloppy` executable was linked against V8
  and any required dynamic runtime files were explicitly included.
- `tools/windows/test-package.ps1 -RequireV8Runtime` and
  `tools/unix/test-package.sh --require-v8-runtime` validate that the package manifest and
  runtime-file staging agree, but that check still does not execute V8 code.
- V8 runtime packaging is validated only when a V8-enabled package is built from a
  V8-enabled executable, dynamic runtime files are staged when required, the package smoke
  runs outside the checkout with V8 runtime validation enabled, and a V8-gated
  `sloppy run --artifacts ... --stdlib <package-root>/lib/sloppy/stdlib/sloppy --once
  GET /` smoke succeeds.

CI policy:

- required pull-request CI does not fetch, build, or require V8;
- manual `workflow_dispatch` can run the optional V8 validation job;
- the optional job requires `enable_v8=true` and a runner-local `v8_root` pointing to a
  preinstalled SDK;
- if no SDK path is supplied or the path does not exist, the job reports skipped/not
  configured and does not claim V8 validation;
- when configured, the job validates the SDK, configures `windows-relwithdebinfo` with
  `SLOPPY_ENABLE_V8=ON`, builds, and runs CTest.

## Tool Layout

```text
tools/
  windows/
    bootstrap.ps1
    dev.ps1
    fetch-v8.ps1
    resolve-v8-sdk.ps1
    v8-sdk.ps1
    build-v8.ps1
    package.ps1
    test-package.ps1
    check-platform-boundaries.ps1
  unix/
    README.md
    check-c-standards.sh
    check-platform-boundaries.sh
    package.sh
```

Root `tools/*.ps1` scripts are compatibility forwarders. Unix shell scripts live under
`tools/unix/`; Linux/macOS CI currently uses the Unix standards scanners and direct
CMake/Cargo commands. The Unix package script is still not part of default required CI.

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
7. write checksum;
8. verify ZIP in a clean directory outside the checkout.

## Install Layout

Local archive layout:

```text
sloppy-<version>-<platform>-<arch>/
  bin/
    sloppy.exe or sloppy
    sloppyc.exe or sloppyc
    # Windows packages also include required native runtime DLLs from vcpkg
  lib/
    sloppy/
      bootstrap/
        sloppy/
          index.js
          app.js
          results.js
          internal/runtime-classic.js
          bootstrap.manifest.json
      stdlib/
        sloppy/
          index.js
          app.js
          results.js
          schema.js
          data.js
          internal/intrinsics.js
          bootstrap.manifest.json
      engines/
        v8/
          # optional runtime DLLs/shared libraries only, never SDK headers/libs
  share/sloppy/
    examples/      # only when requested
    licenses/
    schemas/
  README.md
  LICENSE
  THIRD_PARTY_NOTICES.md
  manifest.json
```

`SHA256SUMS.txt` is written next to the archive, not inside it. Packages are created under
`artifacts/packages/`, which is ignored.

TASK 11.A starts the bootstrap support-data layout. Source assets live in
`stdlib/sloppy/`, the normal build copies them to
`<build>/lib/sloppy/bootstrap/sloppy/`, and CMake install places them under
`<prefix>/lib/sloppy/bootstrap/sloppy/`. The copy is plain file staging: no Node, npm,
bundler, transpiler, or package-manager metadata is involved.

EPIC-24 makes the staged bootstrap root executable in V8-gated `sloppy run`. Build-tree
executables use the staged `<build>/lib/sloppy/bootstrap/sloppy/` path compiled into the
binary unless `--stdlib <dir>` is supplied. Local ZIP/TAR packages currently stage the
source-controlled stdlib root under `lib/sloppy/stdlib/sloppy/`; executable-relative
package lookup is deferred, so package smoke tests that execute V8 bootstrap behavior
should pass that path explicitly. The runtime never reads stdlib assets from `.sdeps/`,
npm, Node resolution, or the current working directory unless the caller explicitly passes
a relative `--stdlib` path.

EPIC-25 package staging copies the source-controlled stdlib assets into
`lib/sloppy/stdlib/sloppy/` inside the archive. Windows packages also copy runtime DLLs
restored by vcpkg from the build tree into `bin/` so `sloppy.exe` starts outside the
checkout. Database drivers such as Microsoft ODBC Driver for SQL Server are not installed
or bundled. CMake install now stages `sloppy`, the bootstrap stdlib support path, the
package stdlib path, and the CMake-built `sloppyc` debug target when it exists. Final
release install formalization is still deferred; `tools/windows/package.ps1` owns the
reviewable Release archive layout and copies the Release `sloppyc` binary from Cargo
output.

## Release ZIP Layout

First distribution target:

```text
sloppy-0.0.0-dev-windows-x64/
  bin/
    sloppy.exe
    sloppyc.exe
  lib/sloppy/stdlib/sloppy/
  share/sloppy/licenses/
  README.md
  LICENSE
  THIRD_PARTY_NOTICES.md
  manifest.json
```

`manifest.json` records the package name, version, platform, arch, configuration, commit,
tool names, stdlib/example/native-runtime/V8-runtime inclusion booleans, and experimental
notes. If git commit detection is unavailable, tooling writes `unknown`.

The Windows ZIP script creates this layout and a sibling `SHA256SUMS.txt`. Scoop, WinGet,
MSI/MSIX, signing, notarization, installers, auto-update, and public release automation are
future work. Homebrew or other Unix package-manager integration comes after Linux/macOS
support is real.

## Linux/macOS TAR Layout

`tools/unix/package.sh` stages the same layout and writes a `.tar.gz` archive plus
`SHA256SUMS.txt` when run on Linux or macOS with suitable build tools.
`tools/unix/test-package.sh` extracts the archive outside the checkout, runs
`sloppy --version`, `sloppy --help`, `sloppyc --version`, and `sloppyc --help`, verifies
stdlib assets, manifest fields, excluded build/dependency directories, and
`SHA256SUMS.txt` when present.

This path is simple and intentionally not part of the required default CI gate yet.
Linux/macOS configure, build, test, Cargo, and standards checks are validated by CI;
Linux/macOS package smoke remains local/manual until a scoped hosted job proves it on
those runners.

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
- missing package inputs;
- missing checksum inputs;
- generated artifact accidentally tracked.

## Testing Requirements

Build/tooling tests currently include:

- bootstrap smoke;
- configure/build/test;
- format-check;
- lint;
- artifact hygiene;
- platform-boundary scanner.
- Linux/macOS POSIX platform and C standards scanners in CI.
- Windows package smoke from an extracted archive outside the checkout.
- Local Linux/macOS package smoke from an extracted `.tar.gz` archive outside the checkout.

Default package smoke unpacks the archive under the system temp directory, runs
`sloppy --version`, `sloppy --help`, `sloppyc --version`, and `sloppyc --help`, verifies
stdlib assets and manifest fields, checks excluded directories are absent, verifies V8 SDK
headers/import libraries are absent, and verifies `SHA256SUMS.txt` when it is present. It
does not require V8, live databases, SQL Server ODBC drivers, provider credentials, a
running server, admin privileges, or global PATH mutation. Passing package smoke must not
be reported as V8 execution, live provider availability, or release readiness.

V8 package smoke is a separate optional evidence category. It requires a V8-enabled build,
an archive whose manifest records the V8 runtime status accurately, package-smoke runtime
file validation when dynamic V8 files are expected, and a V8-gated artifact execution
smoke from the extracted package layout. If any of those pieces is not run, report V8
package validation as skipped or incomplete.

Sanitizer/fuzz gates are planned as opt-in hardening gates:

- ASan first on core/default non-V8 builds where the platform toolchain supports it;
- UBSan next on clang/gcc builds where dependencies and platform libraries are stable;
- no default required sanitizer job until false positives and dependency/runtime behavior
  are understood;
- fuzz targets start with the untrusted parser/resource boundaries that already exist:
  route pattern parsing, HTTP request-head parsing, Plan JSON parsing, diagnostics/source
  map parsing when richer source maps land, and resource table ID decoding/validation;
- fuzz corpora and crash artifacts must stay out of source control except for deliberate
  small regression seeds.

## Implementation Tasks

- Keep root wrappers forwarding to `tools/windows`.
- Stage package outputs under ignored `artifacts/packages/`.
- Limit manifest fields to small deterministic package metadata.
- Use `tools/windows/package.ps1` for Windows ZIP creation and checksum generation.
- Validate Windows archives with `tools/windows/test-package.ps1` outside the checkout.
- Validate Unix archives locally with `tools/unix/test-package.sh` outside the checkout.
- Keep Linux/macOS TAR packaging local/manual until package CI validates it.
- Add packaging CI job after binaries become real.
- Keep Linux/macOS presets limited to honest non-V8 default validation until release,
  sanitizer, or package-smoke jobs are separately scoped.

## Acceptance Criteria

Build/distribution foundation is accepted when:

- Windows preset configures/builds/tests;
- V8 is optional for foundation build;
- dependency manifest has no premature runtime dependencies;
- tool layout separates Windows and Unix scripts;
- generated artifacts are ignored and untracked;
- packaging policy excludes VCS internals and local build output;
- package archives include `sloppy`, `sloppyc`, stdlib assets, README, LICENSE, and
  manifest;
- Windows package archives include vcpkg runtime DLLs needed for the tools to start
  outside the checkout;
- package archives exclude `.git/`, `.sdeps/`, build directories, Cargo targets, V8 SDK
  headers/import libraries, and local IDE files;
- package tooling writes a SHA256 checksum;
- outside-checkout package smoke passes for available CLI commands.
- default package smoke and V8-enabled package smoke are reported separately.
- docs explain root wrapper and `tools/windows` policy consistently;
- review/source archive hygiene is documented before release packaging exists.

## Open Questions

- Whether release ZIP includes debug symbols separately.
- Exact V8 SDK update cadence and verified prebuilt SDK source.
- Whether CMake install should own the final package layout instead of script staging.
- Exact Linux/macOS preset and CI timing.
