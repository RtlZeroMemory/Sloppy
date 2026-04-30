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
- JSON output;
- source map integration;
- subsystem diagnostic expectations;
- examples;
- tests and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- source map parser;
- V8 exception source-map remapping;
- localization;
- IDE protocol integration.

## Current Phase

MAIN1-06 completes the bounded alpha diagnostic renderer surface:

- severity enum;
- small enum-backed diagnostic code model with stable string names;
- user/app source spans with 1-based line and column when present;
- bounded related spans and hints;
- arena-copying diagnostic builder;
- deterministic plain-text renderer;
- deterministic single-object JSON renderer;
- deterministic single-line source-frame renderer when source text is supplied;
- minimal diagnostic redaction helper for common secret-bearing text;
- golden/snapshot tests for plain text, JSON, source frames, and redaction.

ENGINE-22.B adopts the shared bounded string builder for diagnostic text, JSON diagnostic,
and source-frame rendering. The renderers still preflight output size deterministically and
return `SL_STATUS_CAPACITY_EXCEEDED` on bounded builder exhaustion instead of allocating
recursively through diagnostic paths.

ENGINE-12.AB does not add new public diagnostic codes. Async backend failures use existing
machine-checkable statuses: `SL_STATUS_CAPACITY_EXCEEDED` for bounded queue overflow,
`SL_STATUS_INVALID_STATE` for disposed loops or detectable wrong-thread dispatch, and
`SL_STATUS_INTERNAL` for unexpected backend failures. ENGINE-23 maps provider-executor
terminal and admission states to existing stable codes for the deterministic native source:
`SL_STATUS_INVALID_ARGUMENT` for missing provider instance id, provider mismatch, invalid
operation kind, invalid execution mode, or malformed owned-input views,
`SLOPPY_E_ENGINE_CANCELLED` for cancellation, `SLOPPY_E_ENGINE_PROMISE_PENDING` for
deadline/timeout, `SLOPPY_E_ENGINE_BACKPRESSURE` plus `SL_STATUS_CAPACITY_EXCEEDED` for
overflow/admission failure, `SLOPPY_E_APP_LIFECYCLE` for shutdown cancellation, and
provider-specific diagnostic codes for provider failures when a real provider reports one.
ENGINE-23.C/D also record deterministic worker counters for serialized and blocking-pool
provider-like execution: worker start/stop counts, worker failures, completion-post
failures, and late worker completion after the operation is already terminal. Blocking pool
invalid configuration fails with `SL_STATUS_INVALID_ARGUMENT`, queue overload fails with
`SL_STATUS_CAPACITY_EXCEEDED` plus the backpressure diagnostic path, shutdown rejection
fails with `SL_STATUS_CANCELLED`, provider worker failures preserve the provider-specific
diagnostic code supplied by the worker callback, and unsupported worker execution
mode/backend attempts return `SL_STATUS_UNSUPPORTED` before ownership transfer.
ENGINE-23.E/F adds provider-executor admission diagnostics for capability denial through
provider-supplied policy hooks and `SLOPPY_E_PERMISSION_DENIED`; the executor is not
coupled to database-specific policy and keeps terminal outcomes distinguishable:
`SLOPPY_E_ENGINE_CANCELLED` for caller cancellation, `SLOPPY_E_ENGINE_PROMISE_PENDING` for
deadline/timeout, `SLOPPY_E_ENGINE_BACKPRESSURE` for overflow or completion-post pressure,
`SLOPPY_E_APP_LIFECYCLE` for shutdown, and provider-specific diagnostic codes for provider
failures. Late worker completion after cancellation, timeout, or shutdown increments the
late-completion counter and is cleanup-only. Future provider work may add more specific
data-provider async codes, but it must keep cancelled, timed out, overflowed, shutdown,
permission-denied, and provider-failed outcomes distinguishable. Provider executor
diagnostics must not include raw native pointers, V8/libuv implementation details, SQL
parameter values, connection strings, or other secret-bearing payloads.
ENGINE-23.G hardens this into tested native evidence: queue-full, shutdown, invalid
operation/backend, worker failure, operation failure, cancellation, timeout, late
completion, and capability-denial paths have deterministic counters or terminal
diagnostics, and bounded stress smoke verifies redacted diagnostics without treating the
result as performance proof.

This is not the final diagnostics system. The C renderers are stable enough for alpha
tests and tools, but the native `sloppy` CLI does not yet expose a generic
`--diagnostic-format json` flag for every error path.

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
- source text supplied at render time for optional source frames;
- redacted text when the caller knows a value may contain a secret.

Deferred fields include title, structured fixes, subsystem/category metadata, and richer
redaction classification.

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

typedef struct SlDiagSource {
    SlStr path;
    SlStr text;
} SlDiagSource;
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
- `SLOPPY_E_HTTP_TARGET_LIMIT`;
- `SLOPPY_E_HTTP_HEADER_NAME_LIMIT`;
- `SLOPPY_E_HTTP_HEADER_VALUE_LIMIT`;
- `SLOPPY_E_HTTP_HEADER_BYTES_LIMIT`;
- `SLOPPY_E_HTTP_UNSUPPORTED_METHOD`;
- `SLOPPY_E_HTTP_ROUTE_NOT_FOUND`;
- `SLOPPY_E_HTTP_CONNECTION_CLOSED`;
- `SLOPPY_E_HTTP_REQUEST_TIMEOUT`;
- `SLOPPY_E_HTTP_OVERLOAD`;
- `SLOPPY_E_HTTP_KEEP_ALIVE_UNSUPPORTED`;
- `SLOPPY_E_DUPLICATE_ROUTE`;
- `SLOPPY_E_HTTP_UNSUPPORTED_BODY`;
- `SLOPPY_E_INVALID_HTTP_RESULT`;
- `SLOPPY_E_HTTP_BODY_LIMIT`;
- `SLOPPY_E_HTTP_UNSUPPORTED_MEDIA_TYPE`;
- `SLOPPY_E_SQLITE_PROVIDER`;
- `SLOPPY_E_DATABASE_UNSUPPORTED_VALUE`;
- `SLOPPY_E_POSTGRES_PROVIDER`;
- `SLOPPY_E_POSTGRES_POOL_EXHAUSTED`;
- `SLOPPY_E_SQLSERVER_PROVIDER`;
- `SLOPPY_E_SQLSERVER_POOL_EXHAUSTED`;
- `SLOPPY_E_MISSING_SERVICE`;
- `SLOPPY_E_PERMISSION_DENIED`;
- `SLOPPY_E_ENGINE_PROMISE_REJECTION`;
- `SLOPPY_E_ENGINE_PROMISE_PENDING`;
- `SLOPPY_E_ENGINE_CANCELLED`;
- `SLOPPY_E_ENGINE_BACKPRESSURE`;
- `SLOPPY_E_APP_LIFECYCLE`;
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

Source-frame rendering is implemented as a bounded single-line renderer. When callers
provide source text that matches the primary span, the renderer prints:

```text
error SLOPPY_E_INVALID_ROUTE_PATTERN: unsupported dynamic route pattern

  --> app.js:5:12 (len 9)
   |
 5 | app.mapGet(routePath, handler)
   |            ^^^^^^^^^ expected a string literal route pattern
```

If source text is unavailable, the requested line is missing, or the source path does not
match, rendering falls back to the regular deterministic text output. Tabs and non-ASCII
bytes are copied as-is; caret placement is byte-column based. This is intentional for the
alpha renderer and is not an IDE-grade display-width engine.

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

## Redaction Policy

Diagnostics must not print secret values. Secret key names such as `DATABASE_URL` or
`SLOPPY_SQLSERVER_TEST_CONNECTION_STRING` may appear because they tell developers where to
fix configuration; the environment variable value must not appear.

The bounded C helper `sl_diag_redact_secrets` masks common diagnostic-risk strings:
`password`, `pwd`, `token`, `secret`, `api_key`/`apikey`, and URI userinfo passwords such
as `postgres://user:password@host/db`. Provider-specific redaction remains the first line
of defense for PostgreSQL and SQL Server connection strings because those providers know
their connection-string grammar. The generic helper is a safety net for shared diagnostic
paths, not a full data-loss-prevention engine.

CLI doctor text and JSON output use this same helper for connection-string-like check
messages. The helper preserves secret-key names and masks secret values with deterministic
`<redacted>` tokens so text and JSON process goldens cover the same redaction behavior.

## Machine-Readable Output

`sl_diag_render_json` emits one deterministic JSON diagnostic object for tools:

```json
{
  "code": "SLOPPY_E_INVALID_ROUTE_PATTERN",
  "severity": "error",
  "message": "unsupported route",
  "primary": {
    "file": "app.js",
    "line": 5,
    "column": 12,
    "span": 9
  },
  "hints": ["use a string literal route pattern"]
}
```

Field order is stable: `code`, `severity`, `message`, optional `primary`, optional
`related`, optional `hints`. The renderer performs JSON escaping itself, emits no
timestamps or random IDs, and does not include raw pointers. Callers must redact
secret-bearing messages before rendering; JSON output must never include unredacted
secrets. A CLI-wide diagnostic format flag remains deferred until native command error
paths share the renderer consistently.

## Source Map Integration

Runtime exception flow:

1. V8 reports generated JavaScript location;
2. V8 bridge captures exception and stack;
3. diagnostic reports the generated JavaScript file/span when V8 provides one;
4. bounded stack text may appear as related detail when available;
5. runtime source-map consumption and original TypeScript spans remain deferred.

V8 exception source-map remapping remains deferred to ENGINE-08. MAIN1-06 source frames do
not parse source maps, and ENGINE-07 lifecycle/async diagnostics do not rewrite generated
V8 stack locations to original TypeScript locations.

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
- request/app lifecycle state errors and cleanup registration failures;
- request scope leak in debug mode.
- invalid route pattern in the native route parser;
- duplicate route parameter names.
- duplicate route method+pattern pairs during route table construction;
- request target, header count, header name, header value, total header byte, and body
  parser limit failures;
- HTTP backend connection closed/error, request timeout/deadline, overload/backpressure,
  and unsupported keep-alive/body behavior diagnostics;
- unsupported request bodies;
- unsupported request content types;
- request body size limit failures;
- malformed JSON request bodies;
- invalid HTTP result descriptors.

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
- SQLite JavaScript transaction diagnostics distinguish nested transactions, transaction
  use after commit/rollback, active transaction close attempts, stale/closed connection
  handles, capability denial, and native provider failures. When a transaction callback
  throws or rejects, the runtime rolls back automatically and rethrows the original
  callback error rather than wrapping it in a separate rollback diagnostic. They must not
  expose native pointers, SQL parameter values, or secret-bearing configuration.
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

App/request lifecycle diagnostics:

- invalid app lifecycle state, such as cleanup registration before startup, uses
  `SLOPPY_E_APP_LIFECYCLE`;
- app lifecycle JSON diagnostics use the normal deterministic `sl_diag_render_json` field
  order and include no timestamps, random IDs, raw native pointers, or provider handles;
- lifecycle cleanup helpers close resources through `SlResourceTable` IDs rather than
  logging native pointers.

Permissions diagnostics:

- missing capability;
- wrong capability kind;
- insufficient capability access;
- provider mismatch;
- denied filesystem/network skeleton capability;
- denied database token;
- stale resource ID.

MAIN1-10 and ENGINE-23.F use `SLOPPY_E_PERMISSION_DENIED` for deterministic capability
denials. Hints may include token, kind, operation, required/actual access, provider token,
provider instance id/name, and provider kind when safe. They must not include connection
strings, passwords, API keys, raw provider handles, SQL parameter values, or native
pointers.

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
- JSON output;
- redaction behavior;
- source map fallback behavior.

`core.diagnostics.foundation` covers snapshot text, JSON escaping/output, source-frame
output/fallback, stable code/severity mapping, related spans, hints, and representative
secret redaction. Compiler golden diagnostics cover source frames where `sloppyc` already
has source spans. Source-map remapping remains deferred.

## Quality Gates

- every new diagnostic code has a snapshot or explicit test plan;
- snapshots must not contain machine-local paths unless normalized;
- secret-like values are redacted in tests;
- CI fails on snapshot drift unless the update is intentional.

## Development Tasks

1. Define diagnostic code naming policy. Done for the foundation enum/string mapping.
2. Add user/app source span and diagnostic structs. Done as `SlSourceSpan` and `SlDiag`.
3. Add formatter for plain text output. Done for deterministic foundation text and
   source-frame text when source is available.
4. Add snapshot harness. Done with CTest fixture comparisons.
5. Add examples from this document as fixtures. Started with missing service and invalid
   plan version.
6. Add JSON output only after plain text stabilizes. Done for single diagnostics.
7. Add source map mapping after compiler artifacts exist. Deferred to MAIN1-05 for V8
   exception remapping and later compiler span fidelity work.

## Acceptance Criteria

Diagnostics foundation is accepted when:

- C structs represent severity, code, message, primary location, and related notes;
- formatter emits deterministic text;
- JSON formatter emits deterministic valid JSON;
- source-frame formatter emits deterministic single-line frames when source is supplied;
- missing service and invalid plan version examples are covered by snapshots;
- diagnostics can be attached to `SlStatus`-returning operations without replacing
  `SlStatus`;
- output redacts secrets;
- docs and tests define how new diagnostic codes are reviewed.

## Open Questions

- Exact code namespace prefix for released diagnostics.
- Whether CLI-level JSON diagnostic output should be line-delimited, array-based, or part
  of each command's existing JSON envelope.
- How much source map logic lives in C versus compiler/helper code.
- Whether diagnostics support localization later.
