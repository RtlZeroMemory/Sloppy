# Diagnostic Golden Fixtures

These fixtures pin deterministic diagnostic renderer output for the default non-V8 lane.
They cover stable text, JSON, source-frame, and redaction behavior without claiming V8,
live-provider, package, stress, or benchmark evidence.

| Fixture | Lane | Category | Coverage |
| --- | --- | --- | --- |
| `missing_service.snap` | default | service/runtime | Text diagnostic with source location and help. |
| `invalid_plan_version.snap` | default | Plan validation | Text diagnostic for unsupported Plan versions. |
| `json_single.json` | default | JSON renderer | Stable JSON field ordering with related spans and hints. |
| `source_frame.snap` | default | source frame | Text source-frame rendering with a matched source path. |
| `json_source_frame.json` | default | JSON source frame | Machine-readable source-frame rendering. |
| `async_rejection.json` | default | V8/handler/async shape | Stable async diagnostic JSON shape without V8 execution. |
| `capability_denial_source_frame.snap` | default | provider/capability | Safe capability hint and source-frame rendering. |
| `malformed_json_body.json` | default | HTTP/binding/validation | JSON diagnostic with request-body source frame. |
| `provider_failure_redacted.json` | default | provider/config | Redacted provider metadata in JSON output. |
| `runtime_feature_unknown.json` | default | runtime features | Unknown Plan `requiredFeatures[]` id diagnostic. |
| `runtime_feature_unavailable_postgres.json` | default | runtime features | Deferred PostgreSQL feature requested by Plan. |
| `runtime_feature_unavailable_sqlserver.json` | default | runtime features | Deferred SQL Server feature requested by Plan. |
| `runtime_feature_unavailable_sqlite.json` | default | runtime features | SQLite provider requested while availability is disabled. |
| `runtime_feature_v8_disabled.json` | default | runtime features | V8 feature requested from a non-V8 runtime lane. |
| `runtime_feature_missing_transport_dependency.json` | default | runtime features | HTTP activation with unavailable `transport.libuv` dependency. |
| `runtime_feature_unavailable_transport.json` | default | runtime features | Direct `transport.libuv` required-feature availability failure. |
| `runtime_feature_inactive_sqlite_intrinsic.snap` | default | stdlib/runtime features | Stdlib SQLite missing-intrinsic text when `provider.sqlite` is inactive. |

V8-gated exception, async, SQLite users API, and `sloppy run` diagnostic evidence stays in
the V8-enabled CTest lane and its process tests. Default renderer goldens must not be
reported as V8 execution evidence.
