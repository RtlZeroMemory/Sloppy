# Diagnostic Golden Fixtures

These fixtures pin deterministic diagnostic renderer output for the default non-V8 lane.
They cover stable text, JSON, source-frame, and redaction behavior while keeping separate V8,
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
| `runtime_feature_unavailable_crypto.json` | default | runtime features | Direct `stdlib.crypto` required-feature availability failure before crypto backends land. |
| `runtime_feature_unavailable_codec.json` | default | runtime features | Direct `stdlib.codec` required-feature availability failure before codec implementations land. |
| `runtime_feature_unavailable_net.json` | default | runtime features | Direct `stdlib.net` required-feature availability failure before TCP backends land. |
| `runtime_feature_unavailable_http_client.json` | default | runtime features | Direct `stdlib.httpclient` required-feature availability failure before the HTTP client lane is active. |
| `runtime_feature_unavailable_os.json` | default | runtime features | Direct `stdlib.os` required-feature availability failure before OS runtime API implementation lands. |
| `runtime_feature_inactive_sqlite_intrinsic.snap` | default | stdlib/runtime features | Stdlib SQLite missing-intrinsic text when `provider.sqlite` is inactive. |
| `time_timeout.json` | default | Time diagnostics | Timeout/deadline diagnostic JSON shape. |
| `time_cancelled.json` | default | Time diagnostics | Caller cancellation diagnostic JSON shape. |
| `time_timer_disposed.json` | default | Time diagnostics | Disposed timer diagnostic JSON shape. |
| `time_invalid_delay.json` | default | Time diagnostics | Invalid delay diagnostic JSON shape. |
| `time_deadline_expired.json` | default | Time diagnostics | Expired deadline diagnostic JSON shape. |
| `time_interval_overflow.json` | default | Time diagnostics | Bounded interval overflow diagnostic JSON shape. |
| `time_schedule_skipped.json` | default | Time diagnostics | No-overlap skipped scheduled run diagnostic JSON shape. |
| `time_fake_clock_misuse.json` | default | Time diagnostics | Misused or disposed fake-clock diagnostic JSON shape. |
| `crypto_feature_unavailable.json` | default | Crypto diagnostics | Crypto feature unavailable diagnostic JSON shape. |
| `crypto_unsupported_algorithm.json` | default | Crypto diagnostics | Unsupported secure algorithm diagnostic JSON shape. |
| `crypto_insecure_legacy_algorithm.json` | default | Crypto diagnostics | Legacy/insecure algorithm warning JSON shape. |
| `crypto_invalid_key_secret.json` | default | Crypto diagnostics | Invalid key/secret shape diagnostic without secret contents. |
| `crypto_password_verify_failed.json` | default | Crypto diagnostics | Password verification failure diagnostic without password or hash internals. |
| `crypto_password_hash_unsupported.json` | default | Crypto diagnostics | Unsupported encoded password-hash format diagnostic. |
| `crypto_random_source_unavailable.json` | default | Crypto diagnostics | Secure random source fail-closed diagnostic. |
| `crypto_secret_disposed.json` | default | Crypto diagnostics | Disposed Secret stale-use diagnostic shape. |
| `crypto_constant_time_invalid_input.json` | default | Crypto diagnostics | Constant-time comparison input validation diagnostic. |
| `crypto_backend_unavailable.json` | default | Crypto diagnostics | Backend unavailable diagnostic without leaking backend secrets. |
| `crypto_noncrypto_hash_security_context_warning.json` | default | Crypto diagnostics | Warning when NonCryptoHash use looks security-like. |
| `codec_feature_unavailable.json` | default | Codec diagnostics | Codec feature unavailable diagnostic JSON shape. |
| `codec_unsupported_encoding.json` | default | Codec diagnostics | Unsupported encoding diagnostic JSON shape. |
| `codec_invalid_base64.json` | default | Codec diagnostics | Invalid standard Base64 diagnostic JSON shape. |
| `codec_invalid_base64url.json` | default | Codec diagnostics | Invalid Base64Url diagnostic JSON shape. |
| `codec_invalid_hex.json` | default | Codec diagnostics | Invalid hex diagnostic JSON shape. |
| `codec_malformed_utf8.json` | default | Codec diagnostics | Malformed UTF-8 diagnostic JSON shape. |
| `codec_binary_read_out_of_bounds.json` | default | Codec diagnostics | Bounds-checked binary reader diagnostic JSON shape. |
| `codec_binary_invalid_endian_or_field_size.json` | default | Codec diagnostics | Invalid binary endian/field-size diagnostic JSON shape. |
| `codec_compression_backend_unavailable.json` | default | Codec diagnostics | Compression backend unavailable diagnostic JSON shape. |
| `codec_decompression_limit_exceeded.json` | default | Codec diagnostics | Decompression bomb/output-limit diagnostic JSON shape. |
| `codec_compressed_stream_corrupt.json` | default | Codec diagnostics | Corrupt compressed-stream diagnostic JSON shape. |
| `codec_checksum_unsupported_algorithm.json` | default | Codec diagnostics | Unsupported checksum algorithm diagnostic JSON shape. |
| `codec_checksum_security_context_warning.json` | default | Codec diagnostics | Warning when checksum use looks security-like. |
| `net_feature_unavailable.json` | default | Network diagnostics | Network feature unavailable diagnostic JSON shape. |
| `net_connect_denied.json` | default | Network diagnostics | Strict-policy connect denial diagnostic JSON shape. |
| `net_listen_denied.json` | default | Network diagnostics | Strict-policy listen denial diagnostic JSON shape. |
| `net_invalid_host.json` | default | Network diagnostics | Invalid host diagnostic JSON shape. |
| `net_invalid_port.json` | default | Network diagnostics | Invalid TCP port diagnostic JSON shape. |
| `net_dns_failure.json` | default | Network diagnostics | DNS failure diagnostic with endpoint redaction reminder. |
| `net_connect_timeout.json` | default | Network diagnostics | Connect timeout diagnostic JSON shape. |
| `net_connect_cancelled.json` | default | Network diagnostics | Connect cancellation diagnostic JSON shape. |
| `net_connection_closed.json` | default | Network diagnostics | Closed connection diagnostic JSON shape. |
| `net_stale_handle.json` | default | Network diagnostics | Stale TCP handle diagnostic JSON shape. |
| `net_read_write_timeout.json` | default | Network diagnostics | Read/write timeout diagnostic JSON shape. |
| `net_read_write_cancelled.json` | default | Network diagnostics | Read/write cancellation diagnostic JSON shape. |
| `net_backpressure_overflow.json` | default | Network diagnostics | Bounded TCP queue overflow diagnostic JSON shape. |
| `net_unsupported_option.json` | default | Network diagnostics | Unsupported socket option diagnostic JSON shape. |
| `net_backend_unavailable.json` | default | Network diagnostics | Backend unavailable diagnostic without raw handle exposure. |
| `os_feature_unavailable.json` | default | OS diagnostics | OS feature unavailable diagnostic JSON shape. |
| `os_env_access_denied.json` | default | OS diagnostics | Strict-policy environment access denial diagnostic JSON shape. |
| `os_env_secret_redacted.json` | default | OS diagnostics | Environment value redaction warning shape. |
| `os_process_execution_denied.json` | default | OS diagnostics | Strict-policy process execution denial diagnostic JSON shape. |
| `os_shell_execution_denied.json` | default | OS diagnostics | Shell execution denial diagnostic JSON shape. |
| `os_command_not_found.json` | default | OS diagnostics | Command lookup failure diagnostic JSON shape. |
| `os_invalid_cwd.json` | default | OS diagnostics | Invalid process working directory diagnostic JSON shape. |
| `os_invalid_env_override.json` | default | OS diagnostics | Invalid environment override diagnostic JSON shape. |
| `os_process_timeout.json` | default | OS diagnostics | Process timeout diagnostic JSON shape. |
| `os_process_cancelled.json` | default | OS diagnostics | Process cancellation diagnostic JSON shape. |
| `os_process_killed.json` | default | OS diagnostics | Process killed terminal-state diagnostic JSON shape. |
| `os_process_start_failed.json` | default | OS diagnostics | Process start failure diagnostic JSON shape. |
| `os_pipe_closed.json` | default | OS diagnostics | Closed process pipe diagnostic JSON shape. |
| `os_unsupported_platform_signal.json` | default | OS diagnostics | Unsupported platform signal diagnostic JSON shape. |
| `os_signal_handler_failure.json` | default | OS diagnostics | Signal/shutdown handler failure diagnostic JSON shape. |
| `net_local_ipc_feature_unavailable.json` | default | Local IPC diagnostics | LocalEndpoint requested before a Unix socket or named pipe backend is active. |
| `net_local_ipc_unsupported_platform.json` | default | Local IPC diagnostics | Platform-specific UnixSocket/NamedPipe unsupported behavior. |
| `net_local_ipc_invalid_path.json` | default | Local IPC diagnostics | Invalid named-root local endpoint path. |
| `net_local_ipc_path_denied.json` | default | Local IPC diagnostics | Strict local endpoint path policy denial. |
| `net_local_ipc_stale_cleanup_failed.json` | default | Local IPC diagnostics | Stale socket cleanup failure or denial. |
| `net_local_ipc_endpoint_exists.json` | default | Local IPC diagnostics | Existing endpoint without allowed stale cleanup. |
| `net_local_ipc_connect_failed.json` | default | Local IPC diagnostics | Local IPC connect failure without handle exposure. |
| `net_local_ipc_listen_failed.json` | default | Local IPC diagnostics | Local IPC listen failure without raw endpoint internals. |
| `net_local_ipc_accept_cancelled.json` | default | Local IPC diagnostics | Accept cancellation/timeout diagnostic shape. |
| `net_local_ipc_read_write_cancelled.json` | default | Local IPC diagnostics | Read/write cancellation/timeout diagnostic shape. |
| `net_local_ipc_disposed.json` | default | Local IPC diagnostics | Disposed connection/server stale-use diagnostic shape. |
| `net_local_ipc_backend_unavailable.json` | default | Local IPC diagnostics | Backend unavailable diagnostic for platform/backend gaps. |
| `net_local_ipc_permission_unsupported.json` | default | Local IPC diagnostics | Permission/mode unsupported diagnostic shape. |

V8-gated exception, async, SQLite users API, and `sloppy run` diagnostic evidence stays in
the V8-enabled CTest lane and its process tests. Default renderer goldens must not be
reported as V8 execution evidence.
