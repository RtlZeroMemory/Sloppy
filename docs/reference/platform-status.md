# Platform Status Reference

Sloppy is designed to be cross-platform, but current runtime/Plan enforcement is still bounded.

## Current Emitted Target

Compiler-emitted plans currently target:

- `platform: windows-x64`
- `engine: v8`

`sloppy run` checks these target fields and rejects unsupported targets.

## V8 Execution Gate

- Handler execution requires a V8-enabled runtime build.
- Non-V8 lanes can still compile and validate artifacts, and `doctor` reports V8 as a warning state.

## Provider Live Checks

Default local checks are not live-provider passes by default.

Examples from doctor output:

- PostgreSQL live check warns when `SLOPPY_POSTGRES_TEST_URL` is not set.
- SQL Server driver checks can emit explicit missing-driver errors.

## CLI/Tooling Surface

- Canonical contributor workflow scripts are under `tools/windows`.
- Unix scripts exist under `tools/unix`, but this does not change the current runtime target enforcement described above.

## Non-Claims

- No claim that all runtime/provider/package lanes are fully validated on every OS.
- No claim that non-`windows-x64` Plan targets run through current `sloppy run`.
