# CLI

Status: Initial metadata-only introspection implemented.

Purpose: document Sloppy CLI commands for metadata inspection, local readiness checks,
audit findings, and OpenAPI skeleton generation.

Implemented commands:

```powershell
sloppy routes --plan <path> [--format text|json]
sloppy doctor [--plan <path>] [--format text|json]
sloppy audit --plan <path> [--format text|json]
sloppy openapi --plan <path> [--output <path>]
```

These commands inspect plan-compatible metadata JSON only. They do not compile apps, run
handlers, start an HTTP server, enter V8, connect to live databases, or execute arbitrary
application code.

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
