# Sloppy Examples

Examples are split by confidence level. Runnable examples are useful to try by
hand during the public alpha, pre-production period. Fixture examples are small
source shapes kept stable by tests.

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
| `package-zod-like` | package graph fixture | `npm install && sloppy build && sloppy deps .sloppy` from the example directory | Local fixture package resolution from `node_modules`, package `exports`, Program Mode dependency graph | Uses a local `file:` dependency; no registry access required. Runtime execution requires V8. |
| `dependency-graph` | package graph fixture | `npm install && sloppy build && sloppy deps .sloppy --format json` from the example directory | Installed fixture package, `node:path` compatibility shim, `assetInclude`, dependency graph inspection | Uses a local `file:` dependency; no registry access required. |
| `node-compat-path-events` | compile-only / tooling fixture | `sloppy build && sloppy deps .sloppy` from the example directory | Supported `node:path` and `node:events` compatibility shims | Shows explicit shim imports; does not claim full Node runtime behavior. |
| `dynamic-module-include` | compile-only / tooling fixture | `sloppy build && sloppy deps .sloppy` from the example directory | Computed dynamic imports over `moduleInclude` plus asset metadata | Runtime dynamic import succeeds only for modules sealed into the graph. |
| `hello-minimal` | runnable with source input | `sloppy run examples/hello-minimal/src/main.ts --once GET /hello/Ada` | Smallest project/source-input app | V8 lane writes a full HTTP response with `{"hello":"Ada"}` body. |
| `web-dynamic-routes` | runnable with source input | `sloppyc build examples/web-dynamic-routes/app.ts --out .sloppy` | Static and dynamic web route registration with partial Plan metadata | Static route metadata remains complete; dynamic routes emit findings and require V8 for handler execution. |
| `prealpha-control-plane` | contributor/internal app-host and source-input run | `ctest -R "bootstrap.stdlib.prealpha_control_plane_dogfood\|conformance.prealpha_control_plane"` | Multi-file app, modules, CORS, request IDs/logging, ProblemDetails, SQLite-shaped provider, services, health | App-host test passes; V8 source-input lane returns `Compiler Platform`. |
| `testhost-basic` | documentation example | Read `examples/testhost-basic/README.md` | `TestHost.create(app)` fluent request/response assertions | Plain JavaScript example; covered by bootstrap TestHost tests. |
| `testhost-db` | documentation example | Read `examples/testhost-db/README.md` | Test data helper shape for SQLite-backed tests | SQLite execution depends on the active native bridge lane. |
| `request-context` | runnable with `sloppy run --once` | `ctest -R conformance.request_context.*run_once` | Route params, query, method, path, raw target | V8 lane returns JSON request context fields. |
| `auth-api` | compile-only / tooling fixture | `sloppy build && sloppy routes .sloppy && sloppy openapi .sloppy` from the example directory | JWT/API-key/session auth setup, route requirements, roles, policies, OpenAPI security metadata | Uses placeholder secrets; protected-route manual execution needs a request path that can supply headers or cookies. |
| `realtime-dashboard` | compile-only / tooling fixture | `sloppy build examples/realtime-dashboard/app.js --out .sloppy && sloppy routes .sloppy && sloppy openapi .sloppy` | SSE route metadata, WebSocket route intent, and in-process hub shape | SSE uses bounded `Results.stream`; WebSocket execution returns the documented `501` unavailable response. |
| `websocket-echo` | documentation example | Read `examples/websocket-echo/README.md` | App-host WebSocket text/JSON echo, origin policy, subprotocols, heartbeat, idle timeout, and limits | Use with `TestHost.create(app)`; native runtime upgrade execution remains unavailable. |
| `websocket-auth` | documentation example | Read `examples/websocket-auth/README.md` | JWT-protected WebSocket route with required scope | Use with TestHost `.withJwt(...)`; native runtime upgrade execution remains unavailable. |
| `websocket-json-schema` | documentation example | Read `examples/websocket-json-schema/README.md` | Schema-validated JSON WebSocket messages | App-host TestHost behavior only. |
| `websocket-testhost` | documentation example | `node examples/websocket-testhost/test.mjs` | First-party WebSocket TestHost client helpers | Plain JavaScript app-host test; no external WebSocket package required. |
| `users-api-sqlite` | runnable with `sloppy run --once` | `ctest -R conformance.users_api_sqlite.*run_once` | SQLite source-input conformance app | V8/SQLite lane returns seeded users. |
| `framework-hello` | runnable with source input | `ctest -R conformance.framework_hello` | Typed route binding and request context | V8 lane returns `{"hello":"Ada"}`. |
| `framework-di-services` | runnable with source input | `ctest -R conformance.framework_di_services_example.run_once` | Singleton/scoped/transient service injection | V8 lane returns deterministic service values. |
| `framework-sqlite-crud` | runnable with source input | `ctest -R conformance.framework_sqlite_crud` | Typed SQLite provider injection and CRUD shape | V8/SQLite lane returns seeded SQLite users. |
| `configured-api` | compile-only / tooling fixture | `ctest -R "conformance.configured_api\|examples.configured_api"` | Project config and Plan inspection | Emits artifacts and CLI metadata; no positive handler execution claim. |
| `modules-api` | compile-only / tooling fixture | `ctest -R "conformance.modules_api\|examples.modules_api"` | Function module source-input workflow | Emits artifacts and CLI metadata. |
| `validation-errors` | compile-only / tooling fixture | `ctest -R "conformance.validation_errors\|examples.validation_errors"` | Plan validation metadata and OpenAPI/doctor output | Emits artifacts and CLI metadata. |
| `framework-explicit-binding` | compile-only / tooling fixture | `ctest -R conformance.framework_explicit_binding` | `Route`, `Query`, `Body`, `Header`, `RequestContext` binding metadata | Emits artifacts and CLI metadata. |
| `framework-validation-errors` | compile-only / tooling fixture | `ctest -R conformance.framework_validation_errors` | Schema-backed body binding diagnostics | Emits artifacts and CLI metadata. |
| `framework-postgres-crud` | live-provider example | `.\tools\windows\test-live-postgres.ps1` | Typed PostgreSQL provider shape | Optional provider example. Requires V8, PostgreSQL client support, connection-string config, and live PostgreSQL service; default lane skips/unavailable when missing. |
| `framework-sqlserver-crud` | live-provider example | `.\tools\windows\test-live-sqlserver.ps1` | Typed SQL Server provider shape | Optional provider example. Requires V8, Microsoft ODBC Driver 17 or 18, connection-string config, and live SQL Server service; default lane skips/unavailable when missing. |
| `postgres-basic` | live-provider fixture | `.\tools\windows\test-live-postgres.ps1` | PostgreSQL runtime provider bridge | Optional provider fixture. Requires PostgreSQL client support and live PostgreSQL setup. |
| `sqlserver-basic` | live-provider fixture | `.\tools\windows\test-live-sqlserver.ps1` | SQL Server runtime provider bridge | Optional provider fixture. Requires live SQL Server setup and Microsoft ODBC Driver 17 or 18. |
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
| `dogfood` | contributor/internal machine-readable catalog | `.\tools\windows\dogfood.ps1 -StatusOnly -Json` | Scenario vocabulary and lane expectations | Catalog validation; not an app by itself. |
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
- `package graph fixture`: the example demonstrates package/dependency graph
  behavior. Local `file:` dependencies avoid internet access.
- `documentation example`: the example is reader-facing documentation for an
  API shape already covered by focused tests. It is not a standalone execution
  lane.
- `live-provider example`: the example needs an external database and matching
  optional provider dependency. Default Sloppy apps do not need PostgreSQL, SQL
  Server, libpq, or ODBC.
- `API-shape fixture`: static checks keep the example honest about imports,
  APIs, and documentation boundaries.
