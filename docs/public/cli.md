# CLI

Status: Metadata introspection, dev-only artifact run, and direct source-input handoff
implemented for the current supported compiler subset.

Purpose: document Sloppy CLI commands for dev-only artifact execution, metadata
inspection, local readiness checks, audit findings, Plan-derived OpenAPI output, and
future optimization candidate reports.

Implemented commands:

```powershell
sloppy run <source.js>
sloppy run
sloppy run --artifacts <dir> [--stdlib <dir>]
           [--environment Development] [--host 127.0.0.1] [--port 5173]
           [--once METHOD TARGET]
sloppy routes --plan <path> [--format text|json]
sloppy capabilities --plan <path> [--format text|json]
sloppy doctor [--plan <path>] [--format text|json]
sloppy audit --plan <path> [--format text|json]
sloppy openapi --plan <path> [--output <path>]
```

`sloppy run <source.js>` invokes `sloppyc build`, writes artifacts to a deterministic
tool-owned output directory, validates `app.plan.json`, `app.js`, and `app.js.map`, then
enters the same runtime path as `sloppy run --artifacts <dir>`. Runtime execution is still
dev-only and requires a V8-enabled build. Default non-V8 builds may prove the compiler
handoff and artifact validation, but they must not be reported as V8 execution success.
The compiler handoff passes argv through the platform process runner; source paths and
`sloppy.json` values are not interpolated through a shell command string.

`sloppy run` with no source reads `sloppy.json` from the current directory. The supported
project-run config shape is:

```json
{
  "entry": "app.js",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

`entry` is required and resolves relative to `sloppy.json` in the current directory.
`outDir` defaults to `.sloppy`. `environment` defaults to `Development` and selects the
`appsettings.{Environment}.json` overlay for source-input compilation. `sloppy.json` is
project/run configuration only; app configuration lives in `appsettings.json` and
`appsettings.{Environment}.json`. Unknown fields, invalid JSON, non-string values, and
missing `entry` fail clearly.

The other commands inspect `app.plan.json` artifact metadata only. `routes`, `doctor`, and
`openapi` first validate the file through the native Plan v1 parser, then read the same
metadata sections that compiler artifacts carry. `audit` uses the same metadata shape but
is intentionally able to inspect problem fixtures so it can report multiple alpha audit
findings in one run. None of these commands compile apps, run handlers, start an HTTP
server, enter V8, connect to live databases, or execute arbitrary application code.

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
sloppy run app.js --environment Development --host 127.0.0.1 --port 5173
sloppy run --artifacts .sloppy --once GET /
```

`sloppy run` expects an artifact directory containing:

```text
app.plan.json
app.js
app.js.map
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

The explicit artifact path remains supported and is the advanced/debug path:

```powershell
sloppyc build examples/compiler-hello/app.js --out .sloppy-main-smoke
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

Source-input app configuration uses this precedence:

1. built-in defaults;
2. `appsettings.json` next to the source entry;
3. `appsettings.{Environment}.json` next to the source entry;
4. canonical environment variables such as `SLOPPY_SLOPPY__SERVER__PORT`;
5. selected CLI overrides: `--environment`, `--host`, and `--port`.

Environment variables use the `SLOPPY_` prefix and double-underscore separators. Values are
parsed as string/int/bool based on the existing config value where Sloppy knows the key;
invalid values fail during compiler handoff. Secret-like keys are redacted in diagnostics
and Plan metadata.

The direct source shortcut does not bypass artifacts. Positional source input writes to
`.sloppy/cache/dev/source-input` and rebuilds conservatively on each invocation. The
`sloppy.json` form writes to `outDir` and also rebuilds every time in this first slice.
Users can delete `.sloppy/` and `.sloppy/cache/` safely; they are generated outputs. A
future cache-reuse slice must include source/import hashes, compiler/runtime/stdlib
identity, target platform/engine, environment, and feature/options before claiming stale
cache correctness.

Current source input follows the compiler's existing JavaScript policy. `.js` and `.mjs`
can compile when they match the supported source shape. `.ts`/`.mts` are handed to
`sloppyc` so they fail with the compiler's TypeScript-not-supported diagnostic; there is
no full TypeScript typechecker, package-manager behavior, `node_modules` resolution, watch
mode, hot reload, public alpha claim, or benchmark/performance claim in this command.

Default server binding is `127.0.0.1:5173`. The server is single-process, dev-only, and
intentionally tiny: HTTP/1 request heads only, GET dispatch only, route/query/request
context only, no TLS, no body parsing, no headers in handler context, no streaming, no
keep-alive contract, no middleware, no hot reload, no package manager, no npm resolution,
no arbitrary import graph, and no Node compatibility.

`--once METHOD TARGET` is a deterministic dev/test helper. It does not open a socket; it
loads artifacts, builds the native dev route table, dispatches one synthetic request target,
prints the HTTP response bytes, and exits nonzero only for startup/tooling failures. Route
misses print a `404` response and method mismatches print a `405` response. In socket mode,
unsupported transfer/body framing prints `501`, malformed JSON prints `400`, oversized
bodies print `413`, and unsupported content types print `415`.

In non-V8 builds, `sloppy run` fails before serving with:

```text
sloppy run: sloppy run requires V8-enabled build
```

Missing stdlib assets, missing app modules, malformed plans, missing route metadata,
duplicate route method/pattern pairs, invalid route patterns, missing plan handlers, missing
registrations, duplicate handler registrations, intrinsic misuse, V8 evaluation failures,
thrown handlers, malformed query strings, unsupported request framing/content types,
oversized bodies, invalid JSON request bodies, and
malformed/unsupported result descriptors fail with stderr diagnostics or safe dev HTTP
responses depending on whether the failure happens during startup or request dispatch.

## Metadata Input

`--plan <path>` reads a JSON file with the current minimal Sloppy Plan v1 fields plus
optional interim CLI metadata sections:

- `routes`: route method, pattern, numeric `handlerId`, name, module, source location,
  request binding metadata, response metadata, completeness, and inferred effects when the
  compiler emitted them;
- `modules`: module name and dependency names;
- `dataProviders`: provider token, provider name, and matching service token;
- `capabilities`: declared/generated capability token, kind, access, and provider
  reference;
- `doctorChecks`: deterministic check records used by tests or safe metadata producers.

The native Plan parser owns the minimal Plan v1 schema plus alpha route/provider/capability
sections. CLI tests still keep additional fixture-only `modules` and `doctorChecks`
metadata for deterministic audit and doctor coverage until compiler/app-host emission grows
those sections.

## routes

`sloppy routes` validates the plan, then prints route metadata in deterministic
method/pattern order. Text output includes source order, method, pattern, handler ID,
completeness, module, source location, request bindings, response metadata, and name. JSON
output returns the same route facts in a stable `routes` array.

Empty route tables are valid and print only the text header or an empty JSON array.
Malformed metadata and missing paths fail with stderr diagnostics.

## capabilities

`sloppy capabilities` validates the plan, then reports route effects emitted by the
compiler. Normal provider usage is shown as `generated` inference with the route
method/path, provider token, provider kind, capability kind, access, inference reason,
operation, and source location. The command does not infer new permissions and does not
present runtime-only guesses as facts.

## doctor

`sloppy doctor` prints evidence-aware local readiness checks. It checks the build-layout
bootstrap runtime asset, reports whether this binary was compiled with the V8 bridge,
states that live provider checks are not configured by default, and warns that local CLI
checks are not package release readiness. When `--plan` is supplied, the command validates
the file through the native Plan v1 parser and reports route/provider/capability metadata
presence, Plan completeness, partial/runtime-only/invalid route metadata, missing response
metadata, and body JSON bindings that lack schema metadata before including deterministic
`doctorChecks` from the metadata file.

Live provider checks are not run by default. Provider availability that requires external
servers, credentials, or machine-local drivers must stay opt-in in later CLI work.
Connection-string-like check messages are redacted before text or JSON output.
Redaction is shared with the diagnostic helper: secret key names remain visible for
actionability, while secret values are replaced with deterministic `<redacted>` tokens in
both text and JSON output.

## audit

`sloppy audit` runs a small fixed rule set over metadata:

- duplicate route names;
- duplicate route method and pattern pairs;
- routes that reference missing handler IDs;
- route completeness that is not complete;
- body JSON bindings that lack schema metadata;
- unknown response metadata on Strong Plan routes;
- future optimization candidate notes derived from response schema, body schema, and
  provider/effect metadata;
- modules with missing dependencies;
- direct two-module dependency cycles;
- incomplete data provider token/provider/service metadata.

Text and JSON output use stable finding codes. ERROR findings return a nonzero process
exit code so the command can be used in CI/static review; warnings and notes do not fail
the command by themselves. This is not a large rule engine. Future audit rules should be
added with fixtures and source-doc updates.

## openapi

`sloppy openapi` emits an OpenAPI 3.0.3 document from validated Plan metadata for the
currently supported framework subset. It sets default `info.title` and `info.version`,
marks `x-slop-openapi-policy.status` as `plan-supported-subset`, writes deterministic
paths/methods, uses route names as `operationId`, converts route/query/header parameters
when Plan-visible, emits request bodies for schema-backed `ctx.body.json(...)` bindings,
emits known response statuses/helpers from `Results.*` metadata, and includes a validation
problem response component.

The document includes Slop extensions:

- `x-slop-source` for source path/line/column when Plan-visible;
- `x-slop-completeness` for complete/partial/runtime-only/invalid route state;
- `x-slop-capabilities` for provider/effect metadata;
- `x-slop-optimization-candidates` for report-only future native JSON, body validation,
  provider-aware route, and static dispatch candidates.

Partial routes still appear. Unknown body or response metadata is marked with
`x-slop-partial`; the command must not invent schemas. It does not implement OpenAPI
validation, security schemes, native JSON fast paths, route partitioning, multi-isolate
execution, runtime optimization, or public-alpha OpenAPI promises. `--output <path>` writes
the JSON to a file; otherwise the command writes stdout.

Benchmarks are currently exposed through `tools/windows/bench.ps1` and the native
`sloppy_bench` CMake target, not through the public `sloppy` CLI.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/compiler.md`, `docs/modules/plan/README.md`.
## Source Input Modules

`sloppy run <source>` remains a shortcut over compile-to-artifacts followed by the existing
artifact runtime path. The supported source graph is static and compiler-owned: `"sloppy"`,
`"sloppy/providers/sqlite"`, and relative app modules under the source root. Unsupported
bare imports, dynamic imports, Node/npm resolution, `node_modules`, and package-manager
behavior are rejected rather than emulated.
