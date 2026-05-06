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

The foundation phase does not implement localization or IDE protocol integration.

## Current Phase

Post-ENGINE-16 consolidation note: ENGINE-15 completed the current source-map and
diagnostic renderer wave, and ENGINE-16 completed lifecycle/resource diagnostic evidence
for the native helper layer. The next diagnostics/observability source is
`docs/project/post-engine-16-diagnostics-observability-audit.md`: runtime events,
counters, request IDs, access events, provider executor counters, and scope/resource
counter reporting are Roadmap-2 work. V8 diagnostics remain V8-gated; default gates still
do not prove V8 source-map remapping, package execution, live providers, public alpha, or
benchmark claims.

HTTP-25.A/B/C update: HTTP transport diagnostics/counters now distinguish keep-alive close
causes for client `Connection: close`, server-forced close, idle timeout, max requests
reached, unsupported pipelining, and shutdown closing idle keep-alive connections. These
diagnostics are stable Sloppy codes/counters and do not expose libuv handles, socket
pointers, raw native pointers, or secret-bearing values.
HTTP-25.D/E adds stable diagnostics for invalid chunk sizes, chunk-size overflow,
malformed chunk delimiters, missing final chunks on close, decoded body-limit failures,
unsupported transfer encodings, conflicting `Content-Length` plus `Transfer-Encoding`,
rejected trailers, and streaming response backpressure. Diagnostic messages remain bounded
and do not include request body bytes.
HTTP-25.F adds conformance/stress assertions for those diagnostics and counters: keep-alive
idle timeout, max requests reached, chunked decode failures, streaming response
backpressure, shutdown/cancel cleanup, and lifecycle cleanup-once paths. These assertions
remain transport-internal evidence and do not expose libuv handles, socket internals, raw
native pointers, request bodies, response bodies, or secret-bearing values.

MAIN1-06 completes the bounded alpha diagnostic renderer surface:

- severity enum;
- small enum-backed diagnostic code model with stable string names;
- user/app source spans with 1-based line and column when present;
- bounded related spans and hints;
- arena-copying diagnostic builder;
- deterministic plain-text renderer;
- deterministic single-object JSON renderer;
- deterministic single-line source-frame renderer when source text is supplied;
- deterministic JSON source-frame renderer when source text is supplied;
- minimal diagnostic redaction helper for common secret-bearing text;
- golden/snapshot tests for plain text, JSON, source frames, and redaction.

ENGINE-22.B adopts the shared bounded string builder for diagnostic text, JSON diagnostic,
and source-frame rendering. The renderers still preflight output size deterministically and
return `SL_STATUS_CAPACITY_EXCEEDED` on bounded builder exhaustion instead of allocating
recursively through diagnostic paths.

ENGINE-02.E adds source-input run diagnostics at the CLI/compiler handoff boundary.
`sloppy run <source>` distinguishes missing source files, invalid `sloppy.json`, missing
`entry`, compiler unavailable, compiler failure, unsupported compiler source shapes,
artifact validation failures, V8 unavailable, runtime startup failures, and handler
failures. Compiler diagnostics remain owned by `sloppyc`; after artifacts are emitted, the
runtime continues to use the existing Plan/bundle/source-map validation and diagnostic
rendering path.

FRAMEWORK-01.B adds compiler/source-input configuration diagnostics. Missing
`appsettings*.json` files are optional and do not warn. Malformed base or
environment-specific JSON, invalid environment variable values, invalid CLI overrides,
missing required typed values, invalid typed conversions, and missing SQLite provider
database configuration fail clearly with source layer context when available. Diagnostics
and emitted Plan metadata redact values for keys that look like secrets, passwords,
tokens, API keys, or connection strings.

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
ENGINE-26.C/D centralizes native cancellation reason to diagnostic-code mapping in
`sl_cancellation_diag_code`: caller cancellation maps to `SLOPPY_E_ENGINE_CANCELLED`,
deadline/timeout maps to `SLOPPY_E_ENGINE_PROMISE_PENDING`, admission/backpressure maps to
`SLOPPY_E_ENGINE_BACKPRESSURE`, shutdown maps to `SLOPPY_E_APP_LIFECYCLE`, and no
cancellation maps to no diagnostic. Generic async completions that are late because their
owner is already terminal do not dispatch user/provider/V8 continuation code; they may
record the late hook and then follow the deterministic cleanup path.

ENGINE-17.B/D keeps SQLite diagnostic categories distinct on the current synchronous V8
bridge path. Capability denial remains a permission diagnostic before native work; invalid,
stale, closed, and wrong-kind handles remain resource diagnostics without native pointer
values; unsupported parameter values use `SLOPPY_E_DATABASE_UNSUPPORTED_VALUE`; and native
SQLite prepare/step/finalize/constraint failures use `SLOPPY_E_SQLITE_PROVIDER`. SQLite
provider diagnostics may include SQL text and SQLite error text, but never positional
parameter values. Secret values must be passed as parameters, not embedded in SQL text.

ENGINE-15.CD completes the shared C diagnostic renderer surface for machine-readable source
frames and stable code completeness. `sl_diag_render_json_with_source` preserves the normal
single-object JSON field order and inserts `sourceFrame` after `primary` when the caller
supplies matching source text. Async/runtime/provider diagnostics can use the same stable
codes and JSON renderer, but V8 async stack/source-map remapping remains V8-gated and
separate from the default lane.

ENGINE-15.E expands the default diagnostic golden suite with representative async JSON,
capability denial source-frame, malformed request-body JSON, and redacted provider failure
snapshots. These are renderer and diagnostic-contract goldens; V8 exception remapping,
async execution, users API transport, package, live-provider, stress, and benchmark
evidence remain separate lanes.

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
- `SLOPPY_E_HTTP_KEEP_ALIVE_IDLE_TIMEOUT`;
- `SLOPPY_E_HTTP_MAX_REQUESTS_REACHED`;
- `SLOPPY_E_HTTP_PIPELINING_UNSUPPORTED`;
- `SLOPPY_E_HTTP_CHUNK_SIZE_INVALID`;
- `SLOPPY_E_HTTP_CHUNK_SIZE_OVERFLOW`;
- `SLOPPY_E_HTTP_CHUNK_DELIMITER_INVALID`;
- `SLOPPY_E_HTTP_CHUNK_FINAL_MISSING`;
- `SLOPPY_E_HTTP_TRAILERS_UNSUPPORTED`;
- `SLOPPY_E_HTTP_RESPONSE_BACKPRESSURE`;
- `SLOPPY_E_HTTP_SHUTDOWN`;
- `SLOPPY_E_HTTP_TRANSPORT_CONFIG`;
- `SLOPPY_E_HTTP_BIND_FAILED`;
- `SLOPPY_E_HTTP_LISTEN_FAILED`;
- `SLOPPY_E_HTTP_ACCEPT_FAILED`;
- `SLOPPY_E_HTTP_DISPATCH_FAILED`;
- `SLOPPY_E_HTTP_RESPONSE_SERIALIZATION_FAILED`;
- `SLOPPY_E_HTTP_WRITE_FAILED`;
- `SLOPPY_E_HTTP_CLOSE_FAILED`;
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
- `SLOPPY_E_LIFECYCLE_START_FAILED`;
- `SLOPPY_E_LIFECYCLE_ALREADY_STARTED`;
- `SLOPPY_E_LIFECYCLE_NOT_STARTED`;
- `SLOPPY_E_LIFECYCLE_SHUTDOWN_STARTED`;
- `SLOPPY_E_LIFECYCLE_SHUTDOWN_FORCED`;
- `SLOPPY_E_LIFECYCLE_REQUEST_SCOPE_CLOSED`;
- `SLOPPY_E_LIFECYCLE_LATE_COMPLETION_DROPPED`;
- `SLOPPY_E_LIFECYCLE_CLEANUP_FAILED`;
- `SLOPPY_E_LIFECYCLE_LEAK_DETECTED`;
- `SLOPPY_E_LIFECYCLE_IDENTITY_UNAVAILABLE`;
- `SLOPPY_E_UNKNOWN_RUNTIME_FEATURE`, for Plan feature ids that are not in the runtime
  registry;
- `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`, for known features unavailable in the current
  runtime lane;
- `SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING`, for known feature dependencies unavailable
  in the current runtime lane;
- `SLOPPY_E_INTERNAL`.

`SLOPPY_NONE` is available for no-diagnostic cases and `SLOPPY_E_UNKNOWN` is returned for
unknown enum values. `core.diagnostics.foundation` also verifies that every public enum
value through the current last diagnostic code maps to a stable non-unknown string.

Runtime feature catalog entries carry only stable feature ids and Plan/requested-by context;
they must not include native handles, pointers, secrets, provider connection strings, or
package-manager state.

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
`password`, `pwd`, `token`, `secret`, `passphrase`, `private_key`/`privatekey`,
`secret_key`/`secretkey`, `client_secret`/`clientsecret`, `key`, `api_key`/`apikey`,
`connectionString`/`connection_string`, and URI userinfo passwords such as
`postgres://user:password@host/db`. Provider-specific redaction remains the first line of
defense for PostgreSQL and SQL Server connection strings because those providers know their
connection-string grammar. The generic helper is a safety net for shared diagnostic paths,
not a full data-loss-prevention engine.

CLI doctor text and JSON output use this same helper for connection-string-like check
messages. The helper preserves secret-key names and masks secret values with deterministic
`<redacted>` tokens so text and JSON process goldens cover the same redaction behavior.
Plan-driven routes, capabilities, doctor, and audit output must also remain deterministic:
finding/check codes are stable, source locations are emitted when the Plan provides them,
and ERROR audit findings return a nonzero process exit. These commands must not include raw
native pointers, provider handles, request bodies, SQL parameter values, or unredacted
configuration secrets.
OpenAPI output follows the same rule: unknown schemas remain `x-slop-partial`, source and
Plan node metadata are emitted only when present, and optimization candidates are reported
as future hooks rather than runtime behavior.

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
`sourceFrame`, optional `related`, optional `hints`. The normal `sl_diag_render_json`
renderer omits `sourceFrame`; `sl_diag_render_json_with_source` includes it only when a
matching source text is supplied at render time. The renderer performs JSON escaping
itself, emits no timestamps or random IDs, and does not include raw pointers. Callers must
redact secret-bearing messages before rendering; JSON output must never include unredacted
secrets. A CLI-wide diagnostic format flag remains deferred until native command error
paths share the renderer consistently.

## Source Map Integration

Runtime exception flow:

1. V8 reports generated JavaScript location;
2. V8 bridge captures exception and stack;
3. when a validated compiler source map is attached for that generated app source, the V8
   bridge remaps the diagnostic primary span to the author source;
4. when no applicable map exists or the map is malformed, the diagnostic reports the
   generated JavaScript file/span honestly;
5. bounded stack text may appear as related generated-context detail when available.

ENGINE-15.A completes the compiler source-map side for the supported subset: generated
maps include normal Source Map v3 handler mappings plus deterministic `x_sloppy` metadata
for source files, routes, modules, schemas, providers, capabilities, and inferred effects.
ENGINE-15.B consumes the Source Map v3 `mappings` table for V8 compile/eval/call
exception primary spans. Missing maps, malformed maps, and uncovered generated locations
fall back to generated spans instead of inventing author locations. MAIN1-06 source frames
do not parse source maps, and ENGINE-07 lifecycle/async diagnostics do not rewrite
generated V8 stack locations to original TypeScript locations.

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
- HTTP backend shutdown rejection/cancellation diagnostics;
- HTTP backend body-read cancellation, timeout, shutdown, body-limit, unsupported-media,
  and invalid body-length diagnostics;
- HTTP stress/conformance smoke keeps parser-limit, body-limit, unsupported-media,
  overload/backpressure, shutdown, malformed-query, and route/method diagnostics
  deterministic under repeated default non-V8 execution;
- HTTP transport listener config, bind, listen, accept, capacity, and lifecycle diagnostics
  stay deterministic and do not include libuv handles or raw pointer values;
- HTTP transport read/request accumulation diagnostics cover read errors, client
  disconnect during head/body, head too large, malformed request head, unsupported transfer
  encoding, body too large, invalid/incomplete body, unsupported media, unsupported
  pipelined bytes, and invalid connection state. These diagnostics stay redacted and do not
  include libuv handles, socket internals, native pointers, request bodies, or secret
  values;
- HTTP transport dispatch/write diagnostics cover invalid dispatch state, missing dispatch
  callback wiring, response serialization or response-buffer-capacity failure,
  write-start/write-completion failure, and reserved close-after-write lifecycle failure
  when detectable. These diagnostics stay redacted and do not include libuv handles, socket
  internals, native pointers, response bodies, request bodies, or secret values;
- HTTP transport cancellation/timeout/shutdown diagnostics cover client disconnect during
  head/body read as `SLOPPY_E_HTTP_CONNECTION_CLOSED`, header/body/total request timeout as
  `SLOPPY_E_HTTP_REQUEST_TIMEOUT`, write timeout/failure as `SLOPPY_E_HTTP_WRITE_FAILED`,
  and backend shutdown rejection/cancellation as `SLOPPY_E_HTTP_SHUTDOWN` where a request
  lifecycle exists. Timeout responses may be deterministic `408 Request Timeout` responses,
  but diagnostics still avoid libuv handles, socket internals, native pointers, request
  bodies, response bodies, and secret values;
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
summary as a related note when practical. ENGINE-15.B adds bounded source-map remapping for
V8 exception primary spans. V8 async diagnostics still do not attach source-map-remapped
async stacks or source frames.

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
- typed close uses `SLOPPY_E_RESOURCE_WRONG_KIND` and does not run cleanup when the live
  entry kind differs from the expected kind;
- table exhaustion is reported by status as `SL_STATUS_CAPACITY_EXCEEDED` and may use
  `SLOPPY_E_RESOURCE_TABLE_EXHAUSTED` when a higher-level operation materializes a user
  diagnostic.

Resource diagnostics may include operation name and expected/actual resource kind names.
They must not include native pointer values or provider handle addresses.

Runtime feature diagnostics:

- `SLOPPY_E_UNKNOWN_RUNTIME_FEATURE` is emitted when Plan `requiredFeatures[]` names a
  feature id that is not in the runtime registry.
- `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` is emitted when a known feature is required but
  unavailable in the current runtime lane, including non-V8 builds that receive V8-targeted
  runnable artifacts, unavailable/deferred PostgreSQL or SQL Server provider features,
  disabled SQLite provider availability, missing transport availability, the current
  contract-only `stdlib.crypto` feature, and stdlib code that reaches a provider bridge
  whose V8 intrinsic was not registered because the active Plan did not enable that
  feature.
- `SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING` is emitted when a known feature's
  dependency is unavailable.

Feature diagnostics include the stable feature id and Plan/requested-by context when known.
They are produced before runtime feature initialization and must not include native handles,
pointers, secrets, provider connection strings, or package-manager state.
`tests/golden/diagnostics/runtime_feature_*.json` pins the deterministic renderer shape for
unknown, unavailable, V8-disabled, provider, transport, and dependency-missing feature
failures. `runtime_feature_inactive_sqlite_intrinsic.snap` pins the stdlib missing-intrinsic
message for `provider.sqlite`.

CORE-TIME-01.A/B adds stable Time diagnostics, CORE-TIME-01.C/D/G wires the first
V8-gated runtime paths that can produce the timeout/cancellation classes from
`sloppy/time`, and CORE-TIME-01.E/F adds deterministic interval, scheduled-job, and
fake-clock paths. CORE-TIME-01.H reuses those same JavaScript error classes for
filesystem facade `signal`, `deadline`, and `timeoutMs` options; it does not create a
separate filesystem timeout diagnostic family:

- `SLOPPY_E_TIME_TIMEOUT` for timeout/deadline expiry;
- `SLOPPY_E_TIME_CANCELLED` for caller/app/request cancellation;
- `SLOPPY_E_TIME_TIMER_DISPOSED` for disposed timer, interval, job, or fake-clock handles;
- `SLOPPY_E_TIME_INVALID_DELAY` for non-finite, negative, or overflow-prone delay values;
- `SLOPPY_E_TIME_DEADLINE_EXPIRED` for already-expired deadlines where scheduling is required;
- `SLOPPY_E_TIME_INTERVAL_OVERFLOW` for bounded tick/job queue overflow;
- `SLOPPY_E_TIME_SCHEDULE_SKIPPED` for no-overlap scheduled runs skipped by policy;
- `SLOPPY_E_TIME_FAKE_CLOCK_MISUSE` for disposed or misused fake clocks.

Missing or inactive `stdlib.time` uses the runtime-feature diagnostics above. Renderer
goldens cover timeout, cancellation, disposed timer, invalid delay, expired deadline,
interval overflow, skipped scheduled run, and fake-clock misuse shapes.
V8-gated Time evidence covers inactive `__sloppy.time` registration and native delay
Promise settlement. Bootstrap stdlib evidence covers fake-clock disposal, deterministic
delay/timeout/interval completion, skipped scheduled runs, and filesystem pre-cancel /
expired-deadline option behavior.

CORE-CRYPTO-01.C/D/F/H adds stable Crypto diagnostics and JSON goldens for the
feature/model plus random/hash/HMAC/Secret surface. Missing or inactive `stdlib.crypto`
uses the runtime-feature or missing-bridge diagnostic path rather than raw JavaScript
property failures. Crypto-specific codes cover primitive/API failures:

- `SLOPPY_E_CRYPTO_FEATURE_UNAVAILABLE` for crypto API use when the feature/backend lane is
  not active;
- `SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM` for algorithms outside the supported matrix;
- `SLOPPY_E_CRYPTO_INSECURE_LEGACY_ALGORITHM` for legacy/insecure algorithm warnings;
- `SLOPPY_E_CRYPTO_INVALID_KEY_SECRET` for invalid key or secret shape;
- `SLOPPY_E_CRYPTO_PASSWORD_VERIFY_FAILED` for verification failure without password/hash
  internals;
- `SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED` for unsupported encoded password-hash
  formats;
- `SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE` for fail-closed OS CSPRNG failures;
- `SLOPPY_E_CRYPTO_SECRET_DISPOSED` for stale/disposed Secret use;
- `SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT` for invalid byte inputs;
- `SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE` for unavailable vetted backend operations.

Crypto diagnostics must not include passwords, secret bytes, encoded-hash internals, random
output, raw native pointers, V8 handles, or backend-specific secret material. Default
goldens prove deterministic diagnostic shape only; they do not prove randomness quality,
side-channel resistance, password cost, performance, or V8 execution.

Random-source, unsupported-algorithm, HMAC key, stale Secret, and constant-time input
failures are surfaced with stable code-name strings in JS-facing errors. Shape tests and
goldens may include operation names and algorithm ids, but never generated random bytes or
secret material.

CORE-CODEC-01.A/B adds stable Codec diagnostics and JSON goldens for the contract-only
feature/model slice. `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` is the startup/Plan-gating
diagnostic when `stdlib.codec` is required but the runtime feature is not enabled before
execution begins. `SLOPPY_E_CODEC_FEATURE_UNAVAILABLE` is reserved for already-reached
codec API paths when the API surface exists but a specific codec backend or optional lane
is inactive. Other codec-specific codes are reserved for transformation/API failures:

- `SLOPPY_E_CODEC_FEATURE_UNAVAILABLE` for codec API use when the feature/backend lane is
  not active;
- `SLOPPY_E_CODEC_UNSUPPORTED_ENCODING` for encodings outside the supported matrix;
- `SLOPPY_E_CODEC_INVALID_BASE64` for malformed standard Base64 input;
- `SLOPPY_E_CODEC_INVALID_BASE64URL` for malformed Base64Url input;
- `SLOPPY_E_CODEC_INVALID_HEX` for malformed hex input;
- `SLOPPY_E_CODEC_MALFORMED_UTF8` for fatal malformed UTF-8 input;
- `SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS` for bounds-checked reader failures;
- `SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE` for unsupported endian/width
  requests;
- `SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE` for unavailable vetted compression
  backends;
- `SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED` for decompression output limits;
- `SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT` for corrupt compressed input;
- `SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM` for unsupported checksum algorithms;
- `SLOPPY_W_CODEC_CHECKSUM_SECURITY_CONTEXT` for statically visible security-looking
  checksum use.

Codec diagnostics may name operation, encoding, checksum algorithm, backend family, byte
length, and configured limits. They must not include raw tokens, secret-looking values,
native pointers, V8 handles, OS handles, or package-manager state. Default goldens prove
deterministic diagnostic shape only; they do not prove V8 execution, compression backend
availability, streaming behavior, performance, or conformance vectors.

App/request lifecycle diagnostics:

- startup storage/init failure uses `SLOPPY_E_LIFECYCLE_START_FAILED`;
- double start uses `SLOPPY_E_LIFECYCLE_ALREADY_STARTED`;
- cleanup registration, request-scope creation, or shutdown finishing before the lifecycle
  reaches the required state uses `SLOPPY_E_LIFECYCLE_NOT_STARTED`;
- finishing shutdown while active requests are still draining uses
  `SLOPPY_E_LIFECYCLE_SHUTDOWN_STARTED`;
- request scopes closed by forced app shutdown record
  `SLOPPY_E_LIFECYCLE_SHUTDOWN_FORCED` as their terminal diagnostic;
- use after close or bare close before terminal metadata uses
  `SLOPPY_E_LIFECYCLE_REQUEST_SCOPE_CLOSED`;
- late request completions after a terminal request scope use `SL_STATUS_STALE_RESOURCE`
  with `SLOPPY_E_LIFECYCLE_LATE_COMPLETION_DROPPED` and a redacted hint telling callers
  not to touch closed scope state;
- cleanup failures use `SLOPPY_E_LIFECYCLE_CLEANUP_FAILED`;
- test/debug leak assertions use `SLOPPY_E_LIFECYCLE_LEAK_DETECTED`;
- future required app/request identity that cannot be found should use
  `SLOPPY_E_LIFECYCLE_IDENTITY_UNAVAILABLE`;
- app lifecycle JSON diagnostics use the normal deterministic `sl_diag_render_json` field
  order and include no timestamps, random IDs, raw native pointers, or provider handles;
- lifecycle cleanup helpers close resources through `SlResourceTable` IDs rather than
  logging native pointers.

Permissions diagnostics:

- missing capability;
- wrong capability kind;
- insufficient capability access;
- provider mismatch;
- denied filesystem capability;
- denied network skeleton capability;
- denied database token;
- stale resource ID.

MAIN1-10 and ENGINE-23.F use `SLOPPY_E_PERMISSION_DENIED` for deterministic capability
denials. Hints may include token, kind, operation, required/actual access, provider token,
provider instance id/name, and provider kind when safe. They must not include connection
strings, passwords, API keys, raw provider handles, SQL parameter values, or native
pointers.

CORE-FS-01.C/D/H adds deterministic core filesystem diagnostics for invalid path syntax,
unknown roots, named-root traversal, development absolute-path warnings, strict absolute
denials, file errors, and inactive `stdlib.fs` bridge access. CORE-FS-01.E/F routes
Directory, FileHandle, temp, atomic, symlink, and native lock failures through the same status and
bridge rejection path, including stale FileHandle IDs. CORE-FS-01.G adds stale watch
handle, unsupported recursive watch, no-event timeout, and bounded queue overflow status
coverage through the same bridge path. Later CORE-FS-01 slices must add
stable text and JSON goldens for doctor/audit output, permission-denied policy shapes,
unsupported platform behavior, watch overflow rendering, lock contention, and atomic-write
cleanup failures.

CORE-FS-02 separates runtime-owned artifact diagnostics from app-facing filesystem policy
diagnostics. Missing `app.plan.json`, `app.js`, `app.js.map`, bootstrap stdlib assets, and
source-input `sloppy.json` fail through stable CLI messages without exposing raw OS error
strings. These trusted runtime reads may use low-level native filesystem helpers for
platform path conversion, but they are not `stdlib.fs` capability denials and must not be
reported as app filesystem policy evidence.

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

### Missing Compiler-Inferred Provider

```text
error[SLOPPYC_E_MISSING_PROVIDER]: route uses unregistered database provider 'data.main'

help: Register the provider with app.use(...), builder.capabilities metadata, or an
explicit runtime-only escape hatch once that pattern is supported.
```

COMPILER-30.H/I uses this diagnostic when effect inference proves a provider operation but
the Plan has no matching provider/capability registration. This is invalid because the
runtime-required provider truth is missing. Optional metadata gaps are instead represented
in Plan completeness as `partial`.

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
- JSON source-frame output;
- redaction behavior;
- source map fallback behavior.

`core.diagnostics.foundation` covers snapshot text, JSON escaping/output, source-frame
output/fallback, JSON source-frame output, complete stable code registry mapping,
stable code/severity mapping, related spans, hints, representative secret redaction, and
ENGINE-15.E renderer goldens for async, capability, request-body, and provider diagnostic
shapes.
Compiler golden diagnostics cover source frames where `sloppyc` already has source spans.
Compiler source-map goldens cover deterministic `x_sloppy` metadata. V8-gated engine smoke
tests cover source-map remapping, missing-map generated fallback, malformed-map fallback,
and async rejection JSON rendering without reporting V8 success from the default lane.

## Quality Gates

- every new diagnostic code has a snapshot or explicit test plan and maps to a stable
  string name;
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
6. Add JSON output only after plain text stabilizes. Done for single diagnostics and
   source-frame JSON when source is available.
7. Add source map mapping after compiler artifacts exist. Done for V8 exception primary
   spans in ENGINE-15.B; async stack remapping and broader compiler span fidelity remain
   later work.

## Acceptance Criteria

Diagnostics foundation is accepted when:

- C structs represent severity, code, message, primary location, and related notes;
- formatter emits deterministic text;
- JSON formatter emits deterministic valid JSON;
- source-frame formatter emits deterministic single-line frames when source is supplied;
- missing service and invalid plan version examples are covered by snapshots;
- CORE-FS-01.I/J covers filesystem capability visibility through
  `SLOPPY_AUDIT_FILESYSTEM_POLICY_VISIBLE`, `stdlib.fs.capabilities`, and
  `stdlib.fs.watch` doctor/audit goldens;
- CORE-FS-02 covers trusted Plan/bundle/source-map/stdlib/config artifact loading as
  runtime diagnostics rather than app filesystem diagnostics;
- CORE-TIME-01.A/B covers the initial Time diagnostic code registry and representative
  JSON goldens for timeout, cancellation, disposed timer, and invalid delay;
- CORE-TIME-01.C/D/G covers V8-gated native delay settlement, `Time.timeout` and
  cancellation error classes, and inactive `stdlib.time` intrinsic gating;
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
