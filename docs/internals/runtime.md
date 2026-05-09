# Runtime Internals

## Where It Lives

- `src/core/app_host.c`
- `src/core/http_dispatch.c`
- `src/core/features.c`
- `src/cli/cli_run.inc`

## Lifecycle

`sloppy run` loads artifacts, validates the Plan, initializes runtime services,
loads generated JavaScript through the engine bridge when V8 is enabled, and
dispatches requests by handler ID.

## Failure Behavior

Malformed artifacts, unsupported feature requirements, and non-V8 execution of
V8-required handlers fail with diagnostics instead of fake success.
