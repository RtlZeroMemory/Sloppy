# Diagnostics

## Purpose

`SlStatus` is for control flow. Diagnostics are for humans and tools.

Diagnostics are part of Sloppy's product experience. A diagnostic should tell the developer
what failed, where it failed, why it matters, and what to try next. It should also give
tools a stable machine-readable code.

## Scope

This document covers:

- diagnostic data model;
- stable diagnostic codes;
- severity levels;
- source locations and code frames;
- related spans;
- hints and fixes;
- future JSON output;
- source map integration;
- subsystem diagnostic expectations;
- examples;
- tests and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- source map parser;
- JSON diagnostic output;
- localization;
- IDE protocol integration.

## Current Phase

TASK 04.A implements the first diagnostic core:

- severity enum;
- small enum-backed diagnostic code model with stable string names;
- user/app source spans with 1-based line and column when present;
- bounded related spans and hints;
- arena-copying diagnostic builder;
- deterministic plain-text renderer;
- synthetic golden/snapshot tests.

This is not the final diagnostics system. The text format is a foundation test contract,
not a released CLI output contract.

## Future Phase

Diagnostics foundation should be implemented before plan loader, compiler extraction, V8
exceptions, HTTP routing, data providers, and capabilities become complex enough to produce
hard-to-debug failures.

## Data Model

Implemented foundation fields:

- stable code, such as `SLOPPY_E_MISSING_SERVICE`;
- severity;
- primary message;
- optional primary source span;
- related locations;
- hint;
- placeholder redacted text when the caller knows a value is secret.

Deferred fields include title, code frames, structured fixes, subsystem, metadata, JSON,
and redaction classification.

Implemented C shape, simplified:

```c
typedef struct SlDiag {
    SlDiagSeverity severity;
    SlDiagCode code;
    SlStr message;
    SlSourceSpan primary_span;
    SlDiagRelated related[SL_DIAG_MAX_RELATED];
    size_t related_count;
    SlStr hints[SL_DIAG_MAX_HINTS];
    size_t hint_count;
} SlDiag;
```

## Severity Levels

- note: contextual information attached to another diagnostic;
- warning: suspicious but allowed behavior;
- error: current command or startup cannot succeed;
- fatal: process cannot continue safely, such as corrupt internal state or OOM in a critical
  path.

Warnings must not be used to hide correctness failures.

## Stable Codes

Diagnostic codes are public tooling contracts once released. Before public CLI/API release,
TASK 04.A uses a small enum plus string mapping to avoid free-form strings spreading
through the C core.

Implemented foundation codes:

- `SLOPPY_E_INVALID_ARGUMENT`;
- `SLOPPY_E_OUT_OF_MEMORY`;
- `SLOPPY_E_OVERFLOW`;
- `SLOPPY_E_INVALID_PLAN_VERSION`;
- `SLOPPY_E_INVALID_PLAN_FIELD`;
- `SLOPPY_E_DUPLICATE_HANDLER_ID`;
- `SLOPPY_E_MALFORMED_JSON`;
- `SLOPPY_E_UNSUPPORTED_ENGINE`;
- `SLOPPY_E_ENGINE_EXCEPTION`;
- `SLOPPY_E_ENGINE_COMPILE_ERROR`;
- `SLOPPY_E_ENGINE_CALL_ERROR`;
- `SLOPPY_E_INVALID_ROUTE_PATTERN`;
- `SLOPPY_E_DUPLICATE_ROUTE_PARAM`;
- `SLOPPY_E_INVALID_HTTP_REQUEST`;
- `SLOPPY_E_HTTP_HEADER_LIMIT`;
- `SLOPPY_E_HTTP_UNSUPPORTED_METHOD`;
- `SLOPPY_E_HTTP_ROUTE_NOT_FOUND`;
- `SLOPPY_E_SQLITE_PROVIDER`;
- `SLOPPY_E_DATABASE_UNSUPPORTED_VALUE`;
- `SLOPPY_E_POSTGRES_PROVIDER`;
- `SLOPPY_E_POSTGRES_POOL_EXHAUSTED`;
- `SLOPPY_E_SQLSERVER_PROVIDER`;
- `SLOPPY_E_SQLSERVER_POOL_EXHAUSTED`;
- `SLOPPY_E_MISSING_SERVICE`;
- `SLOPPY_E_PERMISSION_DENIED`;
- `SLOPPY_E_INTERNAL`.

`SLOPPY_NONE` is available for no-diagnostic cases and `SLOPPY_E_UNKNOWN` is returned for
unknown enum values.

Once released, changing a code requires an ADR or documented migration.

Code families should be grouped by subsystem:

- `SLP_COMPILER_*`;
- `SLP_PLAN_*`;
- `SLP_PLATFORM_*`;
- `SLP_RUNTIME_*`;
- `SLP_ENGINE_*`;
- `SLP_SERVICE_*`;
- `SLP_MODULE_*`;
- `SLP_DATA_*`;
- `SLP_PERMISSION_*`.

## Source Locations And Code Frames

Source spans may refer to:

- TypeScript source;
- generated JavaScript;
- `app.plan.json`;
- runtime configuration;
- environment variables by key;
- platform/tooling paths.

`SlSourceSpan` is a borrowed source/file name plus optional 1-based line, 1-based column,
and optional length. It is distinct from `SlSourceLoc`, which describes C source call
sites.

When generated artifacts are involved later, diagnostics should prefer original TypeScript
source via source maps and include generated locations as secondary details.

Code frames should be concise and deterministic for snapshot tests. Code frames are
deferred; TASK 04.A renders only `path:line:column` and optional length.

## Related Spans

Related spans are bounded to `SL_DIAG_MAX_RELATED` in TASK 04.A and are required for
diagnostics that involve relationships:

- duplicate route: first declaration and duplicate declaration;
- module cycle: every module edge in cycle;
- missing service: consumer route and missing token provider suggestion;
- handler mismatch: plan entry and bundle registration site.

## Hints And Fixes

Hints are bounded to `SL_DIAG_MAX_HINTS` in TASK 04.A. A hint explains direction. A future
structured fix shows concrete code or command when safe.

Rules:

- do not invent fixes Sloppy cannot verify;
- redact secrets;
- include module/provider names;
- prefer adding a missing module or permission grant over vague advice.

## Machine-Readable Output

Future CLI output should support JSON diagnostics for tools:

```json
{
  "code": "SLP_SERVICE_MISSING",
  "severity": "error",
  "message": "service not registered",
  "primary": {
    "file": "users.ts",
    "line": 18,
    "column": 21
  },
  "metadata": {
    "serviceToken": "data.main",
    "handler": "Users.Create"
  }
}
```

JSON output must never include unredacted secrets.

## Source Map Integration

Runtime exception flow:

1. V8 reports generated JavaScript location;
2. V8 bridge captures exception and stack;
3. runtime maps generated location through `app.js.map`;
4. diagnostic reports original TypeScript file/span;
5. generated JS location appears as related detail;
6. missing source map produces a separate diagnostic quality warning/error depending on
   mode.

## Subsystem Expectations

Compiler diagnostics:

- nonliteral route in static mode;
- unsupported dynamic module pattern;
- invalid service token;
- source map generation failure.

App plan diagnostics:

- unsupported schema version;
- missing required section;
- duplicate handler ID;
- route references missing handler;
- module cycle.

Platform diagnostics:

- missing required SDK/tool;
- unsupported platform backend;
- dynamic library load failure later;
- path normalization failure later.

Runtime diagnostics:

- startup validation failure;
- result conversion failure;
- request scope leak in debug mode.
- invalid route pattern in the native route parser;
- duplicate route parameter names.

V8 diagnostics:

- engine initialization failure;
- JavaScript exception;
- JavaScript compile error;
- JavaScript call boundary failure such as a missing/non-callable smoke function or
  unsupported result type;
- rejected promise;
- handler registration mismatch.

TASK 07.D implements only the basic V8 exception mapping skeleton for the current classic
script/global-function smoke API. The bridge captures message text, generated JavaScript
source name when available, 1-based line/column when V8 provides them, and a bounded stack
summary as a related note when practical. It does not perform source-map remapping, render
code frames, add route/handler context, define a promise rejection policy, or capture rich
async stacks.

Services diagnostics:

- missing service;
- duplicate service token;
- invalid lifetime dependency;
- scoped service requested from singleton.

Modules diagnostics:

- dependency cycle;
- missing dependency;
- duplicate module name;
- dynamic behavior in static plan mode.

Data provider diagnostics:

- provider unavailable;
- missing driver;
- missing config;
- parameter binding failure;
- transaction misuse.
- native SQLite provider failures use `SLOPPY_E_SQLITE_PROVIDER` with provider, operation,
  SQLite error text when available, and SQL text without parameter values;
- native PostgreSQL provider failures use `SLOPPY_E_POSTGRES_PROVIDER` with provider,
  operation, libpq error text where available, and redacted connection configuration for
  open/doctor failures;
- native SQL Server provider failures use `SLOPPY_E_SQLSERVER_PROVIDER` with provider,
  operation, ODBC diagnostic records where available, and redacted connection
  configuration for open/doctor failures;
- native PostgreSQL and SQL Server pool exhaustion use provider-specific pool-exhausted
  diagnostics;
- unsupported database parameter kinds use `SLOPPY_E_DATABASE_UNSUPPORTED_VALUE`.

Resource lifecycle diagnostics:

- invalid/null resource ID or missing slot uses `SLOPPY_E_RESOURCE_INVALID_ID`;
- stale generation uses `SLOPPY_E_RESOURCE_STALE_ID`;
- wrong expected kind uses `SLOPPY_E_RESOURCE_WRONG_KIND`;
- closed current slot uses `SLOPPY_E_RESOURCE_CLOSED`;
- table exhaustion is reported by status as `SL_STATUS_CAPACITY_EXCEEDED` and may use
  `SLOPPY_E_RESOURCE_TABLE_EXHAUSTED` when a higher-level operation materializes a user
  diagnostic.

Resource diagnostics may include operation name and expected/actual resource kind names.
They must not include native pointer values or provider handle addresses.

Permissions diagnostics:

- missing capability;
- denied filesystem path;
- denied database token;
- stale resource ID.

## Examples

### Permission Denied

```text
error[SLP_PERMISSION_FS_READ_DENIED]: filesystem read was denied
  --> app.ts:12:18
   |
12 | await fs.readText("secrets.env")
   |                  ^^^^^^^^^^^^^ permission was not granted for this path
help: grant a filesystem read capability for the required path
```

### Missing Service

```text
error[SLP_SERVICE_MISSING]: service not registered

  Route:
    POST /users

  Handler:
    Users.Create

  Missing service:
    data.main

  Required by:
    users.repo

  Fix:
    builder.addModule(postgres.module({
      token: "data.main",
      connectionString: builder.config.require("DATABASE_URL")
    }))
```

### Duplicate Route

```text
error[SLP_ROUTE_DUPLICATE]: route is already registered
  --> users.ts:33:5
   |
33 | app.mapGet("/users/{id:int}", getUserDuplicate)
   |     ^^^^^^^^^^^^^^^^^^^^^^^^^ duplicate GET /users/{id:int}
note: first registered here
  --> users.ts:12:5
```

### Module Cycle

```text
error[SLP_MODULE_CYCLE]: module dependency cycle detected

  Cycle:
    auth -> users -> billing -> auth

help: remove one dependency edge or split shared services into a separate module
```

### Invalid App Plan Version

```text
error[SLP_PLAN_UNSUPPORTED_VERSION]: app.plan.json schema version is not supported

  Found:
    99

  Supported:
    1

help: rebuild the app with a compatible sloppyc version
```

### Bundle Missing Handler

```text
error[SLP_ENGINE_HANDLER_MISSING]: app bundle did not register required handler

  Handler ID:
    100

  Handler:
    Users.Get

  Expected export:
    __sloppy_handler_100
```

### SQL Server ODBC Driver Missing

```text
error[SLP_DATA_SQLSERVER_DRIVER_MISSING]: SQL Server provider unavailable

  Provider:
    sloppy:data/sqlserver

  Reason:
    Microsoft ODBC Driver for SQL Server was not found.

  Install:
    Microsoft ODBC Driver 18 for SQL Server

  Then run:
    sloppy doctor
```

### Source-Mapped Handler Exception

```text
error[SLP_ENGINE_HANDLER_THROWN]: handler threw an exception
  --> users.ts:41:15
   |
41 | const user = await users.get(route.id)
   |               ^^^^^^^^^^^^^^^^^^^^^^^ database timeout
note: generated location app.js:230:19
```

## Testing Requirements

Diagnostic tests must include:

- snapshot text output;
- stable code verification;
- severity verification;
- source span rendering;
- related spans;
- JSON output later;
- redaction behavior;
- source map fallback behavior.

TASK 04.A implements snapshot text, stable code, severity, source span, related span, hint,
and placeholder redaction tests. JSON, source maps, and code frames remain deferred.

## Quality Gates

- every new diagnostic code has a snapshot or explicit test plan;
- snapshots must not contain machine-local paths unless normalized;
- secret-like values are redacted in tests;
- CI fails on snapshot drift unless the update is intentional.

## Development Tasks

1. Define diagnostic code naming policy. Done for the foundation enum/string mapping.
2. Add user/app source span and diagnostic structs. Done as `SlSourceSpan` and `SlDiag`.
3. Add formatter for plain text output. Done for deterministic foundation text.
4. Add snapshot harness. Done with CTest fixture comparisons.
5. Add examples from this document as fixtures. Started with missing service and invalid
   plan version.
6. Add JSON output only after plain text stabilizes.
7. Add source map mapping after compiler artifacts exist.

## Acceptance Criteria

Diagnostics foundation is accepted when:

- C structs represent severity, code, message, primary location, and related notes;
- formatter emits deterministic text;
- missing service and invalid plan version examples are covered by snapshots;
- diagnostics can be attached to `SlStatus`-returning operations without replacing
  `SlStatus`;
- output redacts secrets;
- docs and tests define how new diagnostic codes are reviewed.

## Open Questions

- Exact code namespace prefix for released diagnostics.
- Whether JSON output is line-delimited or array-based.
- How much source map logic lives in C versus compiler/helper code.
- Whether diagnostics support localization later.
