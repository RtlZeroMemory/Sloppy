# Platform Status Reference

Sloppy is designed to be cross-platform, but current runtime/Plan enforcement is still bounded.

## Current Emitted Target

Compiler-emitted plans currently target:

- `platform: windows-x64`
- `engine: v8`

`sloppy run` checks these target fields and rejects unsupported targets.

## Status Table

| Surface | Current state | Check |
| --- | --- | --- |
| Windows x64 local development | Most complete local development path. | Default Windows checks and V8-enabled Windows checks. |
| Linux local development | Supported by Unix scripts for selected build/test paths. | Unix script checks where available. |
| macOS local development | Product target, less complete today. | Unix script checks where available. |
| Runtime handler execution | Requires V8-enabled runtime artifacts. | V8-enabled checks. |
| Live PostgreSQL/SQL Server providers | Opt-in because they need external services and drivers. | Live-provider integration checks. |

## V8 Execution Gate

- Handler execution requires a V8-enabled runtime build.
- Non-V8 builds can still compile and validate artifacts, and `doctor` reports V8 as a warning state.

## Provider Live Checks

Default local checks do not start live database services by default.

Examples from doctor output:

- PostgreSQL live check warns when `SLOPPY_POSTGRES_TEST_URL` is not set.
- SQL Server driver checks can emit explicit missing-driver errors.

## CLI/Tooling Surface

- Canonical contributor workflow scripts are under `tools/windows`.
- Unix scripts exist under `tools/unix`, but this does not change the current runtime target enforcement described above.

## Current Limits

- Runtime/provider/package checks are not fully validated on every OS yet.
- Current `sloppy run` target enforcement is still centered on `windows-x64`
  Plan targets.
