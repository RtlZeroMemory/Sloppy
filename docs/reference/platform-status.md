# Platform Status Reference

Sloppy is designed to be cross-platform. The public alpha
package set covers Windows x64, Linux x64 glibc, and macOS.

## npm Platform Packages

| Platform | Current alpha status |
| --- | --- |
| Windows x64 | First alpha npm platform package target: `@slopware/sloppy-win32-x64`. |
| Linux x64 glibc | First alpha npm platform package target: `@slopware/sloppy-linux-x64`. Release binaries are built on a glibc 2.31 baseline and validated across Debian-family and Fedora-family glibc images. |
| Linux x64 musl / Alpine | No alpha npm platform package. Requires a future separate musl build/package lane. |
| macOS arm64 | Supported macOS alpha platform lane: `@slopware/sloppy-darwin-arm64`. |
| macOS x64 | Supported macOS alpha platform lane: `@slopware/sloppy-darwin-x64`. |
| Linux arm64 | No alpha npm platform package. Use a source build. |
| Windows arm64 | No alpha npm platform package. Use a source build. |

The root `@slopware/sloppy` launcher selects the matching platform package
for supported alpha package lanes. Linux arm64, Linux x64 musl/Alpine, and
Windows arm64 remain source build lanes.

## Current Emitted Target

Compiler-emitted plans currently target:

- `platform: windows-x64`
- `engine: v8`

`sloppy run` checks these target fields and rejects unsupported artifact
targets. Platform package availability and emitted Plan target metadata are
separate surfaces; this alpha still carries Windows-centered Plan goldens while
the release package scripts also stage Linux x64 glibc and macOS runtime
packages.

## Status Table

| Surface | Current state | How to check it |
| --- | --- | --- |
| Windows x64 local development | Most complete local development path. | Default Windows checks and V8-enabled Windows checks. |
| Linux local development | Supported by Unix scripts for selected build/test paths. | Unix script checks where available. |
| macOS local development | Supported macOS alpha target. | Unix script checks where available. |
| Runtime handler execution | Requires V8-enabled runtime artifacts. | V8-enabled test run. |
| Live PostgreSQL/SQL Server providers | Opt-in because they need external services and drivers. | Integration checks with configured services. |

## V8 Execution

- Handler execution requires a V8-enabled runtime build.
- Non-V8 builds can still compile and validate artifacts, and `doctor` reports V8 as a warning state.

## Provider Integration Checks

Default local checks do not start live database services by default.

Examples from doctor output:

- PostgreSQL live check warns when `SLOPPY_POSTGRES_TEST_URL` is not set.
- SQL Server driver checks can emit explicit missing-driver errors.

## CLI/Tooling Surface

- Canonical contributor workflow scripts are under `tools/windows`.
- Unix scripts exist under `tools/unix`, but this does not change the current runtime target enforcement described above.

## Current Limits

- Runtime/provider/package checks are not fully validated on every non-alpha
  platform yet.
- The Linux npm package is a glibc package. It is not expected to run on
  Alpine/musl until a separate musl package exists.
- Current Plan target enforcement and many checked-in Plan goldens are still
  centered on `windows-x64`.
