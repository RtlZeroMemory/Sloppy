# CLI

Status: Metadata introspection and dev-only `sloppy run` MVP implemented.

Purpose: document Sloppy CLI commands for dev-only artifact execution, metadata
inspection, local readiness checks, audit findings, and OpenAPI skeleton generation.

Implemented commands:

```powershell
sloppy run <artifact-dir>|--artifacts <dir> [--stdlib <dir>]
           [--host 127.0.0.1] [--port 5173] [--once METHOD TARGET]
sloppy routes --plan <path> [--format text|json]
sloppy doctor [--plan <path>] [--format text|json]
sloppy audit --plan <path> [--format text|json]
sloppy openapi --plan <path> [--output <path>]
```

`sloppy run` is a dev-only MVP that loads EPIC-21/24 artifacts, enters V8, loads the
classic bootstrap runtime asset, validates handler registration, dispatches GET routes,
passes a minimal route/query/request context, converts supported `Results.*` descriptors,
and writes HTTP/1.1 responses through the native response writer. It requires a V8-enabled
build. Source input handoff to `sloppyc` is deferred; use `sloppyc build ... --out <dir>`
first, then run the emitted artifact directory.

The other commands inspect plan-compatible metadata JSON only. They do not compile apps,
run handlers, start an HTTP server, enter V8, connect to live databases, or execute
arbitrary application code.

Package creation is currently a repository tool, not a `sloppy` CLI command:

```powershell
.\tools\windows\package.ps1 -Configuration Release
.\tools\windows\package.ps1 -Configuration Release -Smoke
.\tools\windows\test-package.ps1 -PackagePath artifacts\packages\sloppy-0.0.0-dev-windows-x64.zip
```

Packages are experimental development artifacts. They are not public releases, installers,
signed/notarized artifacts, package-manager integrations, or auto-updaters.

## run

Supported forms:

```powershell
sloppy run --artifacts .sloppy
sloppy run --artifacts .sloppy --stdlib build\windows-dev\lib\sloppy\bootstrap\sloppy
sloppy run .sloppy --host 127.0.0.1 --port 5173
sloppy run --artifacts .sloppy --once GET /
```

`sloppy run` expects an artifact directory containing:

```text
app.plan.json
app.js
app.js.map   # optional for this MVP
```

The command loads `app.plan.json` through the native Plan parser, reads the interim
compiler-emitted `routes` metadata section, parses GET route patterns with the native route
matcher, creates a V8 engine, loads `<stdlib-root>/internal/runtime-classic.js`, evaluates
`app.js` as the current classic-script artifact, validates that all plan handler IDs were
registered through `__sloppy_register_handler`, and dispatches requests by numeric handler
ID through the runtime-contract path.

Stdlib lookup is deterministic. `--stdlib <dir>` uses the explicit bootstrap stdlib root;
relative explicit paths are resolved by the process in the usual way, so automation should
prefer absolute paths. Without `--stdlib`, build-tree executables use the CMake-staged
bootstrap root compiled into the binary. Package layouts stage the same assets under
`lib/sloppy/bootstrap/sloppy`; executable-relative package lookup is still deferred, so
package smoke tests may pass that directory explicitly.

Source input handoff to `sloppyc` is deferred. MAIN supports the two-step artifact path:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

MAIN1-01 keeps this as the alpha policy. `sloppy run <source.js>` must not implicitly build
or cache source input until a future scoped task designs the compiler handoff, cache keys,
stale-artifact checks, source diagnostics, and rebuild behavior.

Default server binding is `127.0.0.1:5173`. The server is single-process, dev-only, and
intentionally tiny: HTTP/1 request heads only, GET dispatch only, route/query/request
context only, no TLS, no body parsing, no headers in handler context, no streaming, no
keep-alive contract, no middleware, no hot reload, no package manager, no npm resolution,
no arbitrary import graph, and no Node compatibility.

`--once METHOD TARGET` is a deterministic dev/test helper. It does not open a socket; it
loads artifacts, builds the native dev route table, dispatches one synthetic request target,
prints the HTTP response bytes, and exits nonzero only for startup/tooling failures. Route
misses print a `404` response, unsupported methods print a `405` response, and requests
that declare a body in the socket path print a `501` response.

In non-V8 builds, `sloppy run` fails before serving with:

```text
sloppy run: sloppy run requires V8-enabled build
```

Missing stdlib assets, missing app modules, malformed plans, missing route metadata,
duplicate route method/pattern pairs, invalid route patterns, missing plan handlers, missing
registrations, duplicate handler registrations, intrinsic misuse, V8 evaluation failures,
thrown handlers, malformed query strings, unsupported request bodies, and
malformed/unsupported result descriptors fail with stderr diagnostics or safe dev HTTP
responses depending on whether the failure happens during startup or request dispatch.

## Metadata Input

`--plan <path>` reads a JSON file with the current minimal Sloppy Plan v1 fields plus
optional interim CLI metadata sections:

- `routes`: route method, pattern, numeric `handlerId`, name, and module;
- `modules`: module name and dependency names;
- `dataProviders`: provider token, provider name, and matching service token;
- `doctorChecks`: deterministic check records used by tests or safe metadata producers.

The native Plan parser still owns only the minimal handler-oriented Plan v1 schema. The CLI
metadata sections are an interim fixture/artifact shape until compiler/app-host plan
emission grows real route, module, provider, and doctor metadata.

## routes

`sloppy routes` prints the route table from metadata in deterministic method/pattern order.
Text output includes method, pattern, handler ID, and name. JSON output returns a
`routes` array with method, pattern, `handlerId`, name, and module.

Empty route tables are valid and print only the text header or an empty JSON array.
Malformed metadata and missing paths fail with stderr diagnostics.

## doctor

`sloppy doctor` prints safe local readiness checks. The default check reports bootstrap
stdlib assets. When `--plan` is supplied, the command reports that metadata parsing
succeeded and includes any deterministic `doctorChecks` from the metadata file.

Live provider checks are not run by default. Provider availability that requires external
servers, credentials, or machine-local drivers must stay opt-in in later CLI work.
Connection-string-like check messages are redacted before text or JSON output.

## audit

`sloppy audit` runs a small fixed rule set over metadata:

- duplicate route names;
- duplicate route method and pattern pairs;
- routes that reference missing handler IDs;
- modules with missing dependencies;
- direct two-module dependency cycles;
- incomplete data provider token/provider/service metadata.

This is not a large rule engine. Future audit rules should be added with fixtures and
source-doc updates.

## openapi

`sloppy openapi` emits a minimal OpenAPI 3.0.3 JSON skeleton from route metadata. It sets
default `info.title` and `info.version`, writes paths and methods, uses route names as
`operationId`, converts `{id}` and `{id:int}` path parameters, and emits a placeholder
`200` response.

It does not generate schemas, request bodies, validation metadata, examples, security
schemes, or OpenAPI validation. `--output <path>` writes the JSON to a file; otherwise the
command writes stdout.

Benchmarks are currently exposed through `tools/windows/bench.ps1` and the native
`sloppy_bench` CMake target, not through the public `sloppy` CLI.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/compiler.md`, `docs/modules/plan/README.md`.
