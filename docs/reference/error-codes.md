# Error Codes

Sloppy diagnostic codes are stable C enum values exposed as `SLOPPY_*` names by
`sl_diag_code_name`.

This slice adds runtime-reporting codes for failures that need first-class local evidence:

| Code | Subsystem | Typical phase |
| --- | --- | --- |
| `SLOPPY_E_NATIVE_INVARIANT_FAILED` | core | unknown |
| `SLOPPY_E_PLAN_INVALID` | plan | validate |
| `SLOPPY_E_PLAN_SCHEMA_UNSUPPORTED` | plan | validate |
| `SLOPPY_E_ARTIFACT_HASH_MISMATCH` | artifact | load |
| `SLOPPY_E_ROUTE_ARTIFACT_MISMATCH` | artifact | load |
| `SLOPPY_E_ROUTE_VALIDATE_MISMATCH` | artifact | validate |
| `SLOPPY_E_HTTP_METHOD_NOT_ALLOWED` | http | dispatch |
| `SLOPPY_E_JSON_MALFORMED` | http | parse |
| `SLOPPY_E_JSON_SCHEMA_TYPE_MISMATCH` | http | validate |
| `SLOPPY_E_STREAM_BACKPRESSURE_LIMIT` | stream | dispatch |
| `SLOPPY_E_STREAM_INVALID_STATE` | stream | dispatch |
| `SLOPPY_E_V8_HANDLER_EXCEPTION` | v8 | execute |
| `SLOPPY_E_V8_UNHANDLED_REJECTION` | v8 | execute |
| `SLOPPY_E_V8_FATAL` | v8 | execute |
| `SLOPPY_E_WORKER_FAILED` | worker | execute |
| `SLOPPY_E_PROVIDER_UNAVAILABLE` | provider | execute |
| `SLOPPY_E_PROCESS_SPAWN_FAILED` | cli | execute |
| `SLOPPY_E_PACKAGE_ARTIFACT_MISSING` | package | load/package |
| `SLOPPY_E_RELEASE_ARTIFACT_MISMATCH` | release | release |

The richer report shape is documented in
[`docs/internals/diagnostics-runtime.md`](../internals/diagnostics-runtime.md).
