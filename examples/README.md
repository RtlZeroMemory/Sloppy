# Sloppy Examples

Examples are split by confidence level. Runnable examples are useful to try by
hand. Fixture examples are small source shapes kept stable by tests.

Run commands from the repository root unless a row says otherwise. `sloppy run`
handler execution requires a V8-enabled build.

For a new app, use the packaged templates instead of copying an example:

```powershell
sloppy create my-api --template minimal-api
sloppy create my-tool --template program
```

## Example Inventory

| Example | Status | Command | What it covers | Requirements / expected result |
| --- | --- | --- | --- | --- |
| `compiler-hello` | runnable with `sloppy run --once` | `ctest -R conformance.hello.*run_once` | Compiler artifact execution | V8 lane returns `Hello from Sloppy`. |
| `program-hello` | runnable Program Mode source | `sloppy run examples/program-hello/main.ts -- Ada` | Route-free program Plan, relative module import, args/context, and console stdout | V8 lane prints `hello from sloppy program mode Ada`; non-V8 builds can still compile and inspect `kind: program` artifacts. |
| `program-fs-process` | runnable Program Mode source | `sloppy run` from `examples/program-fs-process` | Program stdlib imports for filesystem and OS/process APIs, package run shape | V8 lane writes under `./tmp`, runs `git --version`, and exits with the child exit code. |
| `hello-minimal` | runnable with source input | `sloppy run examples/hello-minimal/src/main.ts --once GET /hello/Ada` | Smallest project/source-input app | V8 lane writes a full HTTP response with `{"hello":"Ada"}` body. |
| `web-dynamic-routes` | runnable with source input | `sloppyc build examples/web-dynamic-routes/app.ts --out .sloppy` | Static and dynamic web route registration with partial Plan metadata | Static route metadata remains complete; dynamic routes emit findings and require V8 for handler execution. |
| `prealpha-control-plane` | app-host dogfood and source-input run | `ctest -R "bootstrap.stdlib.prealpha_control_plane_dogfood\|conformance.prealpha_control_plane"` | Multi-file app, modules, CORS, request IDs/logging, ProblemDetails, SQLite-shaped provider, services, health | App-host test passes; V8 source-input lane returns `Compiler Platform`. |
| `request-context` | runnable with `sloppy run --once` | `ctest -R conformance.request_context.*run_once` | Route params, query, method, path, raw target | V8 lane returns JSON request context fields. |
| `users-api-sqlite` | runnable with `sloppy run --once` | `ctest -R conformance.users_api_sqlite.*run_once` | SQLite source-input conformance app | V8/SQLite lane returns seeded users. |
| `framework-hello` | runnable with source input | `ctest -R conformance.framework_hello` | Typed route binding and request context | V8 lane returns `{"hello":"Ada"}`. |
| `framework-di-services` | runnable with source input | `ctest -R conformance.framework_di_services_example.run_once` | Singleton/scoped/transient service injection | V8 lane returns deterministic service values. |
| `framework-sqlite-crud` | runnable with source input | `ctest -R conformance.framework_sqlite_crud` | Typed SQLite provider injection and CRUD shape | V8/SQLite lane returns seeded SQLite users. |
| `configured-api` | compile-only / tooling fixture | `ctest -R "conformance.configured_api\|examples.configured_api"` | Project config and Plan inspection | Emits artifacts and CLI metadata; no positive handler execution claim. |
| `modules-api` | compile-only / tooling fixture | `ctest -R "conformance.modules_api\|examples.modules_api"` | Function module source-input workflow | Emits artifacts and CLI metadata. |
| `validation-errors` | compile-only / tooling fixture | `ctest -R "conformance.validation_errors\|examples.validation_errors"` | Plan validation metadata and OpenAPI/doctor output | Emits artifacts and CLI metadata. |
| `framework-explicit-binding` | compile-only / tooling fixture | `ctest -R conformance.framework_explicit_binding` | `Route`, `Query`, `Body`, `Header`, `RequestContext` binding metadata | Emits artifacts and CLI metadata. |
| `framework-validation-errors` | compile-only / tooling fixture | `ctest -R conformance.framework_validation_errors` | Schema-backed body binding diagnostics | Emits artifacts and CLI metadata. |
| `framework-postgres-crud` | live-provider example | `.\tools\windows\test-live-postgres.ps1` | Typed PostgreSQL provider shape | Requires V8, libpq, connection-string config, and live PostgreSQL service; default lane skips/unavailable when missing. |
| `framework-sqlserver-crud` | live-provider example | `.\tools\windows\test-live-sqlserver.ps1` | Typed SQL Server provider shape | Requires V8, ODBC driver, connection-string config, and live SQL Server service; default lane skips/unavailable when missing. |
| `postgres-basic` | live-provider fixture | `.\tools\windows\test-live-postgres.ps1` | PostgreSQL runtime provider bridge | Requires live PostgreSQL setup. |
| `sqlserver-basic` | live-provider fixture | `.\tools\windows\test-live-sqlserver.ps1` | SQL Server runtime provider bridge | Requires live SQL Server setup and ODBC driver. |
| `codec-base64-hex` | API-shape fixture | `ctest -R examples.codec.api_shape` | Base64/Base64Url/Hex helpers | Static example check only. |
| `codec-checksums` | API-shape fixture | `ctest -R examples.codec.api_shape` | CRC/checksum helper boundary | Static example check only; not authentication. |
| `codec-compression` | API-shape fixture | `ctest -R examples.codec.api_shape` | gzip/gunzip helper shape | Static example check only. |
| `codec-streaming-compression` | API-shape fixture | `ctest -R examples.codec.api_shape` | streaming compression shape | Static example check only. |
| `codec-text-binary` | API-shape fixture | `ctest -R examples.codec.api_shape` | Text and binary reader/writer helpers | Static example check only. |
| `config-basic` | compile-only / tooling fixture | `ctest -R examples.config.api_shape` | Config binding and defaults | Source build and static example checks; no positive handler execution claim. |
| `config-secrets-redaction` | compile-only / tooling fixture | `ctest -R examples.config.api_shape` | Secret wrapper/redaction boundary | Source build and static example checks; `doctor` reports missing required secret config until configured. |
| `config-strict-mode` | compile-only / tooling fixture | `ctest -R examples.config.api_shape` | Strict config reads | Source build and static example checks; no positive handler execution claim. |
| `core-config-secrets` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Core config/secret policy shape | Static example check only. |
| `core-fs-time-codec` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Combined filesystem/time/codec API shape | Static example check only. |
| `core-network-time-codec` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Combined network/time/codec API shape | Static example check only. |
| `core-policy-audit` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Capability/audit policy shape | Static example check only. |
| `core-process-time-codec` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Process/time/codec API shape | Static example check only. |
| `core-worker-time` | core integration fixture | `ctest -R examples.core_integration.api_shape` | Worker/time API shape | Static example check only. |
| `crypto-hash-hmac` | API-shape fixture | `ctest -R examples.crypto.api_shape` | Hash/HMAC/Secret helpers | Static example check only. |
| `crypto-password` | API-shape fixture | `ctest -R examples.crypto.api_shape` | Async password hashing helpers | Static example check only. |
| `crypto-random-token` | API-shape fixture | `ctest -R examples.crypto.api_shape` | Random token helpers | Static example check only. |
| `crypto-secret-constant-time` | API-shape fixture | `ctest -R examples.crypto.api_shape` | Secret cleanup and constant-time compare shape | Static example check only. |
| `data-foundation` | API-shape fixture | `ctest -R examples.data_foundation.api_shape` | Provider/capability descriptors | Static example check only. |
| `dogfood` | machine-readable catalog | `.\tools\windows\dogfood.ps1 -StatusOnly -Json` | Dogfood scenario vocabulary and lane expectations | Catalog validation; not an app by itself. |
| `ergonomics` | API-shape fixture | `ctest -R examples.ergonomics.api_shape` | Route groups, Results helpers, config/log/services shape | Static example check only. |
| `framework-controller` | API-shape fixture | `ctest -R examples.framework.api_shape` | Controller mapper and DI shape | App-host fixture; compiler source input covers the static controller mapping subset in compiler fixtures. |
| `fs-basic` | API-shape fixture | `ctest -R examples.fs.api_shape` | Directory/File helpers and deadline option | Static example check only. |
| `fs-roots-policy` | API-shape fixture | `ctest -R examples.fs.api_shape` | Logical root path shape | Static example check only. |
| `fs-streams` | API-shape fixture | `ctest -R examples.fs.api_shape` | File stream/line iteration shape | Static example check only. |
| `fs-watch` | API-shape fixture | `ctest -R examples.fs.api_shape` | Directory watch shape | Static example check only. |
| `hello` | hello fixture | `ctest -R examples.hello.api_shape` | Smallest app-host hello shape | Static example check only. |
| `http-client-basic` | API-shape fixture | `ctest -R bootstrap.stdlib.http_client` | Outbound `HttpClient` helper shape | Bootstrap test uses local test bridge; runtime bridge lanes are separate. |
| `modules-basic` | API-shape fixture | `ctest -R examples.modules_basic.api_shape` | Module phases and route contribution shape | Static example check only. |
| `net-deadline-cancel` | API-shape fixture | `ctest -R examples.net.api_shape` | Network cancellation shape | Static example check only. |
| `net-local-ipc` | API-shape fixture | `ctest -R examples.net.api_shape` | Local IPC shape | Static example check only. |
| `net-policy-strict` | API-shape fixture | `ctest -R examples.net.api_shape` | Network policy shape | Static example check only. |
| `net-tcp-client` | API-shape fixture | `ctest -R examples.net.api_shape` | TCP client shape | Static example check only. |
| `net-tcp-echo` | API-shape fixture | `ctest -R examples.net.api_shape` | TCP echo shape | Static example check only. |
| `net-tcp-server` | API-shape fixture | `ctest -R examples.net.api_shape` | TCP server shape | Static example check only. |
| `os-runtime-api` | API-shape fixture | `ctest -R examples.os.api_shape` | OS/environment/process/signal API shape | Static example check only. |
| `sqlite-basic` | provider fixture | `ctest -R examples.sqlite_basic.api_shape` | SQLite provider API shape | Static example check; runtime SQLite lanes are separate. |
| `time-basic` | API-shape fixture | `ctest -R examples.time.api_shape` | Clock/time helpers | Static example check only. |
| `time-deadline-cancellation` | API-shape fixture | `ctest -R examples.time.api_shape` | Deadline/cancellation shape | Static example check only. |
| `time-fake-clock` | API-shape fixture | `ctest -R examples.time.api_shape` | Fake clock shape | Static example check only. |
| `time-interval-schedule` | API-shape fixture | `ctest -R examples.time.api_shape` | Interval/schedule shape | Static example check only. |
| `workers-background-service` | curated worker fixture | `ctest -R examples.workers.api_shape` | Background service shape | Static example check only. |
| `workers-js-isolate` | API-shape fixture | `ctest -R examples.workers.api_shape` | Worker isolate shape | Static example check only. |
| `workers-shutdown` | API-shape fixture | `ctest -R examples.workers.api_shape` | Worker shutdown shape | Static example check only. |
| `workers-workerpool` | curated worker fixture | `ctest -R examples.workers.api_shape` | Worker pool shape | Static example check only. |
| `workers-workqueue` | API-shape fixture | `ctest -R examples.workers.api_shape` | Work queue shape | Static example check only. |

## Status Meanings

- `runnable with sloppy run --once`: the example is expected to execute through
  generated artifacts and V8 in a configured V8 build.
- `compile-only / tooling fixture`: the example is expected to compile and feed
  Plan-backed CLI tools, but no positive handler execution is claimed.
- `live-provider example`: the example needs an external database/driver and
  may be skipped or unavailable on a default machine.
- `API-shape fixture`: static checks keep the example honest about imports,
  APIs, and documentation boundaries.
