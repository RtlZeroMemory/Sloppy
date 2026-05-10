# Platform Status Reference

Sloppy is cross-platform on the alpha runtime targets, but current
runtime/Plan target enforcement is still bounded.

## Current Emitted Target

Compiler-emitted plans currently set:

- `platform: <host platform target>`
- `engine: v8`

`sloppy run` checks these target fields and rejects unsupported targets.

## Published npm platform packages

| Platform | Architecture | Status |
| --- | --- | --- |
| Windows | x64 | published alpha npm platform package |
| Linux (glibc) | x64 | published alpha npm platform package |
| macOS | x64, arm64 | published alpha npm platform package |
| Linux | arm64 | source/archive builds |
| Windows | arm64 | source/archive builds |

## Status Table

| Surface | Current state | How to check it |
| --- | --- | --- |
| Windows x64 local development | Most complete local development path. | Default Windows checks and V8-enabled Windows checks. |
| Linux x64 local development | Supported by Unix scripts for selected build/test paths. | Unix script checks where available. |
| macOS local development | Supported by Unix scripts for selected build/test paths. | Unix script checks where available. |
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

- Runtime/provider/package checks are not fully validated on every OS yet.
- Live PostgreSQL and SQL Server checks need explicit local services and
  drivers and are opt-in.
