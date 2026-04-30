# End-to-End Conformance Suite

Status: MAIN1-13 executable conformance layout.

This suite protects the alpha-supported workflow through the real toolchain:

```powershell
sloppyc build <source.js> --out <artifacts>
sloppy run --artifacts <artifacts> --once GET /
```

Default non-V8 tests compile supported sources, verify deterministic artifact sets, and
exercise unsupported compiler/runtime failure paths. V8-gated tests compile and execute
supported artifacts through `sloppy run --artifacts --once` when the build enables V8.

Conformance cases:

| Area | Source fixture | Default evidence | V8-gated evidence | Deferred or gated requirements |
| --- | --- | --- | --- | --- |
| Hello | `examples/compiler-hello/app.js` | `conformance.hello.compile_artifacts` verifies deterministic `app.plan.json`, `app.js`, and `app.js.map`. | `conformance.hello.run_once` compiles and runs `GET /`, expecting `Hello from Sloppy`. | V8 SDK required for execution. |
| Request context | `examples/request-context/app.js` | `conformance.request_context.compile_artifacts` verifies deterministic artifacts. | `conformance.request_context.run_once` checks route params, repeated query last-wins, method, path, and raw target. | Body/header context remains unsupported. |
| ENGINE-02 compiler metadata | `compiler/tests/fixtures/http-methods/input.js`, `async-handler/input.js`, `provider-capability/input.js`, `source-map/input.js` | Compile-artifact tests verify deterministic method metadata, async metadata, provider/capability plan metadata, and source-map artifacts. | Runtime execution is intentionally not claimed by these fixtures. | Non-GET dispatch, Promise settlement, SQLite bridge enforcement, and source-map remapping remain later ENGINE layers. |
| Results | `examples/compiler-hello/app.js`, `examples/request-context/app.js`, and `tests/integration/execution/invalid_descriptor/` | Compiler and run-path negative fixtures remain default/non-V8 where startup does not need V8. | Hello covers `Results.text`; request-context covers `Results.json`; `conformance.results.invalid_descriptor` expects safe `500`. | Custom headers, streaming, files, redirects, and HTML conversion remain deferred. |
| Unsupported behavior | `compiler/tests/fixtures/unsupported-*`, `tests/fixtures/run/*` | Dynamic route, bare import, unsupported async handler body, and secret-bearing capability conformance reject before artifact success; existing `sloppy.run.*` tests cover source-input deferral, missing/malformed artifacts, V8-disabled diagnostics, and plan drift. | Unsupported method/result cases return safe dev responses when V8 is enabled. | Unsupported request bodies are covered by HTTP parser/dispatch unit tests until socket-mode conformance grows. |
| Capability | `tests/conformance/capability/README.md` | Native registry tests cover denied/missing/insufficient capability policy before fake provider work. | JavaScript bridge enforcement is explicitly deferred. | SQLite JS bridge has not wired the native capability hook yet. |
| SQLite | `tests/integration/execution/sqlite_bridge/` | Native SQLite provider tests cover in-memory provider behavior. | `conformance.sqlite.bridge` runs the V8-gated `:memory:` SQLite JS bridge and expects the selected row. | PostgreSQL and SQL Server JS bridges remain deferred. |

These tests are not benchmark evidence, live provider evidence, production HTTP evidence,
package release evidence, or Node/npm compatibility evidence.
