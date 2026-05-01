# HTTP Module

## Status

Partially implemented.

TASK 10.A adds a native route pattern parser and matcher foundation. TASK 10.B adds the
first llhttp dependency integration skeleton and a complete-buffer HTTP/1 request-head
parser. TASK 10.C adds a synthetic in-memory GET dispatch helper that maps a parsed request
head and manual route binding to a numeric Sloppy Plan handler ID, then calls the existing
runtime-contract/engine boundary. EPIC-22 adds a dev-only `sloppy run` path that now enters
through the reusable HTTP transport boundary for one complete request per connection.
EPIC-23 adds the first native response
descriptor/writer and minimal request context for that path: route params, query params,
and method/path/rawTarget are passed to V8 handlers, supported descriptors become HTTP/1.1
bytes, and the connection is closed. MAIN1-04 hardens that dev-only path with a native
route table built from Plan route metadata, deterministic literal-before-parameter
precedence, request-target/query/body bounds, GET/POST/PUT/PATCH/DELETE dispatch, request
headers and JSON/text bodies in the V8 request context, unsupported content-type and JSON
diagnostics, and stricter result descriptor diagnostics. ENGINE-04 completes the bounded
framework HTTP runtime slice for realistic local APIs. ENGINE-22.A adopts the shared
memory/string primitives in the current HTTP hot paths without changing public behavior:
segmented request target/header accumulation uses builders, body accumulation and response
writing use byte builders, and request/route-owned strings use the core arena copy helpers.
ENGINE-13.A/B/C adds the first proper HTTP backend foundation underneath the dev path:
backend/listener state, connection states, request lifecycle states, parser target/header
limits, timeout/deadline hooks, bounded admission/backpressure counters, and deterministic
HTTP lifecycle diagnostics. ENGINE-13.D/E adds a backend-owned bounded body reader,
body-read cancellation/timeout/shutdown transitions, shutdown rejection for new request
work, active-request shutdown cancellation hooks, and stable shutdown diagnostics.
ENGINE-13.F adds bounded default non-V8 stress and conformance smoke over the implemented
parser, lifecycle, body-policy, overload, shutdown, dispatch, and diagnostic behavior.
ENGINE-24.A/B adds the first reusable transport listener foundation: Slop-owned server
config/state, libuv-isolated TCP bind/listen/accept, bounded accepted-connection
storage, overflow close behavior, and cleanup-once stop/dispose. ENGINE-24.C starts
the accepted-connection read loop and accumulates real TCP chunks into one bounded parsed
request-ready state using the existing ENGINE-13 parser and body policy. ENGINE-24.D
consumes request-ready state through a narrow dispatch callback, serializes
`SlHttpResponse` values with the existing response writer, writes bytes back through libuv,
and closes the TCP connection after the write. ENGINE-24.E adds transport cancellation,
timeout, and shutdown semantics: client disconnect during head/body read cancels and
closes safely, header/body/total-request/write timers transition to terminal cleanup,
timeout can write deterministic `408 Request Timeout` when the socket is still writable,
server stop rejects new accepted work and immediate-cancels/drain-lite closes active
connections, and late callbacks are cleanup-only. ENGINE-24.F/#417 adds bounded localhost
TCP transport smoke/conformance for the implemented MVP path: raw client bytes, simple GET,
route miss, method mismatch, POST text body success, malformed input, body limits,
unsupported media, unsupported `Transfer-Encoding`, close-after-response, one request per
connection, and shutdown cleanup. ENGINE-24.G/#418 recorded close-after-response as the
ENGINE-24 MVP. HTTP-25.A/B/C upgrades that path to bounded sequential HTTP/1.1
keep-alive: HTTP/1.1 reuses connections by default, `Connection: close` forces close,
HTTP/1.0 closes by policy, idle timeout and max requests bound reuse, and request-owned
lifecycle state is reset after each completed response before the next request can
dispatch. Pipelining remains unsupported and is rejected/closed deterministically.
HTTP-25.D/E adds bounded `Transfer-Encoding: chunked` request decoding into the existing
full-body request storage and the first internal/native chunked streaming response writer.
Request streaming APIs, public `Results.stream`, SSE/WebSockets/file streaming, and #446
keep-alive/streaming stress evidence remain separate work. This is not V8 transport
conformance, benchmark evidence, production graceful-drain evidence, or production-edge
HTTP evidence.
ENGINE-17.E adds a V8-gated users API proof over `sloppy run --artifacts` and real
localhost TCP requests for SQLite-backed GET/POST JSON handlers. That proof was
evidence-only and did not introduce keep-alive. HTTP-25.A/B/C later makes the localhost
transport sequentially keep-alive capable, so the same path can now carry a small app
through compiler artifacts, request parsing/body policy, V8 handler execution, SQLite
calls, response serialization, and TCP response bytes across bounded sequential requests.
It does not add public response streaming APIs, TLS, HTTP/2/3, WebSockets, middleware, benchmark evidence, or
production-edge HTTP readiness.
ENGINE-19.BC registers the implemented HTTP evidence under explicit conformance names.
`conformance.http.default_dispatch` runs the synthetic default non-V8 dispatch suite,
`conformance.transport.localhost_mvp` runs the loopback TCP transport MVP suite, and
`conformance.v8.http_dispatch_execution` runs only in V8-enabled builds for synthetic HTTP
dispatch through real V8 handlers. These registrations are evidence organization, not new
HTTP behavior.
There is still no production HTTP server, TLS, HTTP/2, HTTP/3, WebSockets, streaming parser
API, middleware, cookies/sessions, static file server, compression, multipart upload,
public streaming response helpers, public TypeScript `app.run`, or broad response framework.

Framework request/response ergonomics are locked in
`docs/project/framework-api-shape.md`: explicit `ctx.route`, `ctx.query`, `ctx.header`, and
`ctx.body.json(schema)` helpers first; safe validation problem responses; explicit
`Results.*` descriptors first; buffered/non-streaming responses before files or streams.
The current HTTP transport remains sequential-only. It supports bounded full-body chunked
request decoding and an internal/native chunked streaming response descriptor, but it does
not expose request streaming, public JS response streaming helpers, pipelining, SSE,
WebSockets, file streaming, or production HTTP behavior.

## Purpose

Provide native HTTP parsing, routing, and dispatch once prerequisite runtime contract work
exists.

## Scope

Implemented now:

- parse one native route pattern into arena-owned segments;
- match one path against one parsed pattern;
- capture route parameters in deterministic order;
- parse one complete in-memory HTTP/1 request head with llhttp;
- map supported request methods into `SlHttpMethod`;
- copy request target, path, header names, and header values into a caller-provided arena;
- dispatch parsed in-memory GET/POST/PUT/PATCH/DELETE requests through route metadata to a
  Sloppy Plan handler ID;
- invoke the matched handler through the context-aware runtime-contract helper when an
  engine is available;
- convert supported handler descriptors into `SlHttpResponse` values for `sloppy run`;
- dev-only `sloppy run --artifacts <dir>` server over the reusable HTTP transport
  boundary;
- deterministic `sloppy run --artifacts <dir> --once METHOD TARGET` synthetic dispatch;
- startup route table construction from Plan v1 route metadata;
- deterministic route precedence: literal patterns before parameter patterns, stable
  source order when precedence is equal;
- native `SlHttpResponse` descriptors for text, JSON, empty, and problem responses;
- deterministic HTTP/1.1 response writing with status line, managed `Connection` policy,
  `Content-Type`, `Content-Length`, CRLF formatting, body bytes, and 204 no-body behavior;
- minimal query parsing with `%XX`/`+` decoding, last-wins repeated keys, and
  `SL_HTTP_DEFAULT_MAX_QUERY_PARAMS` pair bounds;
- route/query/request/header/body context materialization for V8 handler calls;
- bounded JSON/text request body policy with deterministic 400/413/415/501 failures before
  handler execution;
- request cancellation/backpressure checks before V8 handler entry when a cancellation token
  or in-flight request cap is present;
- ENGINE-22.A memory/string adoption for the current complete-buffer parser, request-owned
  body storage, native response writer, and route string copy/match edge coverage.
- ENGINE-13.A/B/C backend state model with explicit init/start/stop/dispose, listener
  platform boundary, accepted/open/reading/dispatching/writing/closing/closed/error
  connection states, request lifecycle states, bounded active connection/request admission,
  parser target/header/body limits, timeout hooks over `SlCancellationToken`, and stable
  lifecycle/backpressure diagnostics.
- ENGINE-13.D/E body reader and shutdown model: platform/backend code can append bounded
  body chunks into request-arena storage, reject over-limit bodies deterministically, reject
  unsupported media before dispatch, observe cancellation/deadline/shutdown before and
  during body reads, reject new request work after shutdown starts, and cancel active
  request work through deterministic cleanup-once terminal paths.
- ENGINE-24.E transport terminal model: libuv timers cover header read, body read, total
  request, and response write phases; disconnects during read cancel/close without entering
  V8; shutdown stops accepting and closes active transport connections using the backend
  shutdown token path when a request lifecycle exists. The policy is immediate-cancel/
  drain-lite, not production graceful drain.
- ENGINE-13.F stress/conformance smoke evidence: bounded repeated valid requests, repeated
  malformed requests, repeated parser-limit failures, repeated body/media policy failures,
  overload rejection without queue growth, shutdown rejection/cancellation cleanup, and
  default non-V8 conformance-style dispatch diagnostics.
- ENGINE-24.A/B transport listener foundation: Slop-owned HTTP transport server config and
  state, internal libuv TCP listener ownership, localhost bind/listen, accept callback,
  accepted connection placeholders in a bounded table, capacity overflow close, and
  stop/dispose cleanup.
- ENGINE-24.C transport read/request accumulation: accepted connections start a libuv read
  loop, TCP chunks append into bounded per-connection byte-builder storage, request heads
  are detected at `CRLFCRLF`, Content-Length bodies are accumulated through existing
  ENGINE-13 body-reader semantics, parsed requests transition to an internal
  request-ready state, and an internal ready hook can observe the parsed request for tests.
  If no hook is configured in this slice, the parsed request is closed immediately so
  connection/request admission does not remain parked.
- ENGINE-24.D transport dispatch/write: request-ready connections transition through the
  backend dispatch/write lifecycle exactly once, a narrow internal dispatch callback
  returns an `SlHttpResponse`, the existing response writer serializes bytes into
  per-connection storage, libuv writes those bytes to TCP, and the response buffer remains
  alive until the write callback.
- HTTP-25.A/B/C keep-alive lifecycle: after a successful HTTP/1.1 response write, eligible
  connections reset request-owned state and return to an idle/read-wait state for one next
  sequential request. `Connection: close`, HTTP/1.0, disabled keep-alive config, shutdown,
  unsafe error responses, and max-request exhaustion close after write. Idle timeout closes
  cleanly once. Pipelining and concurrent requests on one connection are not supported.
- HTTP-25.D/E chunked/streaming lifecycle: `Transfer-Encoding: chunked` requests are
  decoded before dispatch into the same bounded body bytes used by `ctx.request.text()` and
  `ctx.request.json()`. Chunk trailers are rejected. Internal/native streaming responses
  write `Transfer-Encoding: chunked`, one bounded frame at a time, followed by a final zero
  chunk; keep-alive resumes only after that final chunk completes.

Future scope:

- streaming HTTP parser state;
- production-edge HTTP proof beyond ENGINE-13.F's bounded smoke;
- production server hardening if explicitly scoped later;
- public request streaming API;
- public JavaScript `Results.stream` helper;
- #446 keep-alive/streaming stress and conformance beyond the current smoke;
- route table/trie or other optimized dispatch structure;
- production HTTP response conversion and writing beyond the current dev MVP.

The current real application proof is not a benchmark. ENGINE-17.E covers the HTTP +
SQLite users API path as a V8-gated localhost transport test. ENGINE-19.A defines the
broader conformance matrix in `docs/project/engine-19-conformance-matrix.md`; later
ENGINE-19 slices expand executable HTTP/async cases without treating localhost transport
proof as production-edge HTTP evidence.

EPIC-14 module routes are bootstrap `app.mapGet` registrations only. They do not connect
module routes to the native HTTP parser, synthetic dispatch helper, route params, or
response writer.

## Non-goals

- No production HTTP server, streaming parser, middleware, broad public
  TypeScript API, or app host behavior in TASK 10.B, TASK 10.C, EPIC-22, or EPIC-23.
- No sockets, request/response objects, middleware, route groups, route table, trie,
  precedence engine, public TypeScript API, `app.mapGet`, validation, OpenAPI, V8, or
  compiler extraction in TASK 10.A or TASK 10.B.
- No route params passed to JavaScript, request context, route groups, route precedence,
  middleware, `Results.*`, response formatting, or public TypeScript API in TASK 10.C.
- EPIC-13 route groups exist only in `stdlib/sloppy/app.js` as in-memory bootstrap
  metadata. They do not alter the native route parser, matcher, synthetic dispatch helper,
  HTTP route table, or request context.
- ENGINE-13.A/B/C does not add TLS, HTTP/2, HTTP/3, WebSockets, static files, compression,
  reverse proxy behavior, production benchmark claims, V8/provider/compiler changes, or
  Node/npm compatibility.
- ENGINE-24.A/B does not add request reading, request accumulation, HTTP parsing from TCP
  chunks, response writing, route dispatch, V8 handler execution, provider work, TLS,
  HTTP/2/3, WebSockets, keep-alive, pipelining, streaming, static files, compression,
  reverse proxy behavior, benchmark claims, or public alpha docs.
- ENGINE-24.C does not add response writing, route dispatch, V8 handler execution,
  SQLite/provider work, keep-alive, HTTP pipelining, chunked/streaming bodies, TLS,
  HTTP/2/3, WebSockets, static files, compression, reverse proxy behavior, benchmark
  claims, or public alpha docs.
- ENGINE-24.D does not add timeout/shutdown expansion beyond write cleanup, localhost full
  conformance, users API proof, SQLite/provider work, keep-alive, pipelining, chunked or
  streaming response bodies, TLS, HTTP/2/3, WebSockets, static files, compression, reverse
  proxy behavior, benchmark claims, public alpha docs, or a default V8 transport success
  claim.
- HTTP-25.A/B/C does not add pipelining, concurrent requests on one connection, chunked
  request decoding, streaming request bodies, streaming response writing, SSE, WebSockets,
  TLS, HTTP/2/3, reverse-proxy behavior, static files, compression, benchmark claims,
  public alpha docs, framework binding changes, provider work, or production-edge HTTP
  behavior.

## Public/Internal API

`include/sloppy/route.h` exposes the internal native route foundation:

- `SlRoutePattern`;
- `SlRouteSegment`;
- `SlRouteParam`;
- `SlRouteMatch`;
- `sl_route_pattern_parse`;
- `sl_route_pattern_match`.

`include/sloppy/http.h` exposes the internal HTTP request-head skeleton:

- `SlHttpMethod`;
- `SlHttpHeader`;
- `SlHttpRequestHead`;
- `SlHttpParseOptions`;
- `sl_http_parse_request_head`.

`include/sloppy/http_dispatch.h` exposes the synthetic TASK 10.C dispatch helper:

- `SlHttpRouteBinding`;
- `SlHttpDispatchTable`;
- `sl_http_dispatch_request_head`.

`include/sloppy/http_backend.h` exposes the ENGINE-13.A/B/C backend foundation:

- `SlHttpBackend`;
- `SlHttpBackendOptions`;
- `SlHttpListener`;
- `SlHttpConnection`;
- `SlHttpRequestLifecycle`;
- `SlHttpBodyReader`;
- backend, listener, connection, and request state enums;
- body-reader state enum;
- init/start/stop/dispose;
- connection admission/close/fail;
- request begin, parse, dispatch, write, complete, fail, timeout, and close hooks.
- bounded body-reader begin/append/finish/close hooks;
- request cancellation and shutdown hooks.

`include/sloppy/http_transport.h` exposes the ENGINE-24.A/B transport listener foundation:

- `SlHttpTransportConfig`;
- `SlHttpTransportServer`;
- `SlHttpTransportConnection`;
- server and connection state enums;
- bounded read/request accumulation caps;
- internal request-ready callback for #414/#415 handoff tests;
- narrow internal dispatch callback for #415 transport dispatch/write integration;
- init/listen/poll/stop/dispose;
- blocking run loop for CLI integration;
- accepted connection close;
- internal/test request-byte feed helper;
- bounded active-connection and bound-port query helpers.

Libuv types and handles are not exposed by this header; they remain in
`src/platform/libuv/http_transport_libuv.c`.

Supported route pattern subset:

- `/`;
- static segments such as `/users` and `/users/profile`;
- string parameters such as `/users/{id}` and `/users/{name:str}`;
- integer parameters such as `/users/{id:int}`.

Unsupported route pattern syntax:

- query strings;
- catch-all/wildcard parameters;
- optional segments;
- regex constraints;
- route groups;
- method matching;
- route precedence;
- percent decoding.

Parameter names must start with an ASCII letter or underscore. Remaining characters may be
ASCII letters, digits, or underscores. Parameter names must be unique within one pattern.

Integer parameters match only non-empty ASCII decimal digits. Signs and integer conversion
are deferred.

Trailing slashes are strict: `/users` does not match `/users/`, and `/` matches only `/`.
Empty segments are rejected in patterns except for the root pattern `/`. Empty path segments
do not match.

Paths passed to the matcher must start with `/` and must not include a query string.

The HTTP parser accepts a complete HTTP/1.x request message in one `SlBytes` buffer. It
does not expose streaming state. It rejects malformed input, incomplete input, missing HTTP
versions, unsupported methods, empty/non-path request targets, request targets above
`max_target_length`, header counts above `max_headers`, header names above
`max_header_name_length`, header values above `max_header_value_length`, total header
callback bytes above `max_total_header_bytes`, and bodies above `max_body_length`. When
parse options are omitted, `SL_HTTP_DEFAULT_MAX_HEADERS`,
`SL_HTTP_DEFAULT_MAX_TARGET_LENGTH`, `SL_HTTP_DEFAULT_MAX_HEADER_NAME_LENGTH`,
`SL_HTTP_DEFAULT_MAX_HEADER_VALUE_LENGTH`, `SL_HTTP_DEFAULT_MAX_TOTAL_HEADER_BYTES`, and
`SL_HTTP_DEFAULT_MAX_BODY_LENGTH` are used. A zero header count limit allows requests with
no headers and rejects the first parsed header. Zero values for the other limit fields use
the defaults.

Supported method mapping is intentionally small: GET, POST, PUT, DELETE, PATCH, OPTIONS,
and HEAD. Other llhttp methods fail as unsupported for this skeleton.

`raw_target` stores the request target exactly as reported by llhttp. `path` stores the
portion before `?`. The parser accepts origin-form path targets that start with `/`;
asterisk-form and absolute-form targets are rejected before returning success. Query
parsing is performed by dispatch with `%XX` and `+` decoding. URL normalization and host
validation are deferred.

The dispatch helper accepts an already parsed `SlHttpRequestHead`, a route table, a parsed
`SlPlan`, and an `SlEngine`. The route table is built from Plan v1 route metadata before
serving and parses each route pattern up front. GET, POST, PUT, PATCH, and DELETE entries
are runnable when compiler/plan metadata marks them supported. Duplicate method+pattern
pairs are rejected as startup route-table failures. Literal routes sort before parameter
routes, and equal-precedence routes keep source order. The helper returns `404` for a route
miss, `405` when the path matches but the method does not, validates the handler ID exists
in the plan before entering the engine, parses the query string, applies the body policy,
and then calls `sl_runtime_contract_call_handler_with_context`.

V8 request context materialization exposes route parameters as strings in `ctx.route`,
last-wins query parameters in `ctx.query`, `ctx.request.method`, `ctx.request.path`,
`ctx.request.rawTarget`, case-insensitive request headers through
`ctx.request.headers.get(name)`, deterministic header entries through
`ctx.request.headers.entries()`, `ctx.request.text()`, and `ctx.request.json()` for JSON
bodies. Duplicate request headers with the same case-insensitive name are comma-joined in
insertion order.

`sloppy run` builds that route table from the Plan `routes` section. It uses the native
response writer: supported handler results use their descriptor status, route misses return
`404`, method mismatches return `405`, malformed JSON returns `400`, oversized bodies return
`413`, unsupported content types return `415`, unsupported transfer/body framing returns
`501`, and safe dev `500` text is used for malformed requests, malformed result
descriptors, or handler/runtime failures.

The backend foundation has an explicit lifecycle:

```text
backend: uninitialized -> initialized -> started -> stopping/stopped -> disposed
connection: accepted/open -> reading request -> dispatching -> writing response -> closing
            -> closed or error
request: created -> reading -> dispatching -> writing response -> completed/cancelled/
         timed out/failed -> closed
```

`SlHttpBackend` owns only counters, limits, and state. `SlHttpListener` carries an opaque
platform listener pointer but no socket or OS handle type. `SlHttpConnection` owns one
admitted connection slot until close/fail and preserves connection id/request count across
sequential keep-alive requests. `SlHttpRequestLifecycle` owns one active request admission
slot and a cancellation token; parsed request-head memory remains in the caller's request
arena and is cleared before the arena is reset for the next keep-alive request. The current
transport keeps HTTP/1.1 connections alive by default, but only sequentially: no second
request dispatch begins until the first response write has completed. Production
keep-alive tuning, graceful drain, pipelining, and streaming remain deferred.

## Ownership/Lifetime Rules

Parsed route patterns copy source text, static segment text, and parameter names into the
caller-provided `SlArena`. Parsed pattern views remain valid until that arena is reset or
its backing storage ends.

Successful route match arrays are allocated from the caller-provided match arena. Failed
matches return no parameter array. Captured parameter names point into the parsed pattern
arena. Captured parameter values are borrowed slices of the matched path input and are valid
only while that path storage remains valid.

HTTP request-head data returned by `sl_http_parse_request_head` is arena-owned, including
copied header names, header values, request target/path, and bounded body bytes. The caller
must keep the arena backing storage alive for the desired request-head lifetime. The parser
does not return pointers into llhttp temporary state. It also does not retain pointers to
the input buffer after success. Segmented llhttp callback data is accumulated in
`SlStringBuilder`/`SlByteBuilder` state tied to the request arena; finished header
name/value views are copied into stable request-owned arena storage before the next header
builder reset.

`SlHttpRequestLifecycle` borrows the request arena and never lets request-scope parsed
memory escape the request lifecycle. Admission counters are released exactly once by
complete/fail/timeout/close paths. Timeout hooks cancel the request token with
`SL_CANCELLATION_REASON_DEADLINE_EXCEEDED`; real timer wakeups are not wired in
ENGINE-13.D/E.

`SlHttpBodyReader` is a backend/platform helper, not a JavaScript streaming body API. It
copies body chunks into the request arena through a bounded byte builder with the backend's
`max_body_length` policy. Non-empty bodies currently support `application/json`,
`application/*+json`, and `text/plain`; empty bodies need no content type. Multipart,
file uploads, compression, transfer-encoded streaming, and JS-visible streaming request
bodies remain unsupported. Successful body bytes borrow request-arena storage and are
cleared when the request closes or the arena resets. Cancellation, timeout, shutdown,
unsupported media, over-limit, and content-length mismatch failures reset body-reader
allocations, clear the request body view, transition the request to a terminal cleanup path,
and release the active-request admission slot exactly once.

Shutdown is currently a bounded drain/cancel policy, not a production graceful-drain
implementation. `sl_http_backend_stop` stops accepting new connections and new request work
on existing connections, moves the backend/listener to stopping, and reaches stopped only
after active connection and request counters drop to zero. Active requests may complete
normally, time out, fail, close, or be cancelled through the request shutdown hook. There is
no real drain timer, signal handling, socket half-close policy, or production graceful-drain
claim. ENGINE-13.F/#324 covers only bounded deterministic shutdown smoke for the implemented
core state model.

Native response writing uses `SlByteBuilder` over the caller-provided output buffer. The
returned `SlBytes` view borrows that buffer, and failed writes reset the returned view to an
empty span while preserving deterministic status behavior.

Transport listener storage is arena-owned for the server lifetime. `sl_http_transport_server_init`
copies the bind host as a NUL-terminated boundary adapter for libuv and allocates fixed
listener/connection storage from the caller arena. Accepted connection placeholders own one
backend admission slot until explicitly closed or until server stop/dispose closes them.
Per-connection libuv TCP/timer handles are independently closed by the transport layer;
JavaScript never receives raw pointers or native handles.

Each transport connection owns fixed server-arena storage for read chunks, request-byte
accumulation, response bytes, and a request arena. The accumulation buffer is driven
through `SlByteBuilder`, not raw unbounded allocation. The parsed request head and
accumulated body borrow the connection request arena until response serialization finishes
or the connection is closed. ENGINE-24.D writes response bytes from connection-owned
storage that remains valid until the libuv write callback. HTTP-25.A/B/C preserves those
ownership boundaries for sequential keep-alive by resetting accumulated bytes, parser
state, body-reader state, response storage, request lifecycle state, and the request arena
between requests. The TCP handle, connection id, server reference, request count, idle
timer, counters, and bounded read buffer remain connection-owned.

Transport timeout responses are serialized through the existing `SlHttpResponse` writer and
connection-owned response buffer. Timeout diagnostics do not expose socket internals. If a
timeout fires after the connection is already terminal, the callback is cleanup-only.

HTTP dispatch tables borrow route bindings, parsed route patterns, plans, and engine
handles for the duration of the call only. `sl_http_dispatch_request_head` does not retain
request, route, plan, engine, route-parameter, query-parameter, header, or body storage.
Successful V8 text/JSON/problem response bodies and custom response headers are copied into
the caller-provided arena.

## Invariants

Route parsing is bounded and cannot rely on unbounded recursion. TASK 10.A uses fixed
maximums for one pattern: `SL_ROUTE_MAX_SEGMENTS` and `SL_ROUTE_MAX_PARAMS`.

The route foundation is pure core C: no OS APIs, V8 types, llhttp, libuv, sockets, or heap
allocation are introduced by route parsing/matching.

The HTTP parser skeleton depends on llhttp through vcpkg/CMake. The parser itself uses no
OS-specific headers, libuv headers, or direct OS APIs. Libuv use belongs to the reusable
transport implementation under `src/platform/libuv/` and does not integrate with `SlLoop`.

The dispatch helper is pure core C over existing parser, route, plan, runtime-contract, and
engine boundaries. It does not include V8 headers, expose V8 handles, call OS APIs, create
sockets, drive libuv, allocate outside the caller arena, or build a production router
object. The dev-only CLI server path enters through `SlHttpTransportServer` instead of
using libuv types or handles directly.

The backend foundation is also pure core C. It does not include OS headers, libuv types,
V8 types, provider handles, or direct socket APIs. Concrete accept/read/write/timer work
must enter through platform/runtime layers in later ENGINE-13 slices.

## Diagnostics

Implemented parser diagnostics are intentionally small:

- malformed/invalid route pattern;
- duplicate route parameter name.

TASK 10.B adds small HTTP parser diagnostics:

- invalid/malformed/incomplete HTTP request;
- HTTP header count limit exceeded;
- HTTP header name/value/total-byte limit exceeded;
- request target length exceeded;
- request body length exceeded.

ENGINE-13.A/B/C adds backend diagnostics:

- HTTP connection closed/error;
- HTTP request timeout/deadline;
- HTTP overload/backpressure;
- unsupported keep-alive/body behavior where encountered.

ENGINE-13.D/E adds backend body/shutdown diagnostics:

- HTTP body limit;
- unsupported request media type;
- invalid body length;
- cancellation during body read;
- timeout during body read;
- shutdown while reading, dispatching, or writing;
- shutdown rejection for new request work.

TASK 10.C, MAIN1-04, and ENGINE-04 add small synthetic/dev dispatch diagnostics:

- unsupported or method-mismatched dispatch method;
- unsupported request body;
- unsupported request content type;
- malformed JSON request body;
- no matching route;
- matched route references a missing plan handler;
- duplicate route table entries during startup;
- malformed result descriptors returned by handlers;
- missing/non-callable/throwing JavaScript functions through the existing engine diagnostic
  path.

Future diagnostics:

- ambiguous routes beyond the current literal-vs-parameter precedence policy;
- request conversion errors;
- route conflict/source-span diagnostics.
- route group/source metadata diagnostics once compiler extraction and plan route sections
  exist.
- keep-alive idle timeout, max-requests close, client close while idle or between
  sequential requests, and shutdown drain/force-close diagnostics for a future HTTP/1.1
  upgrade.

ENGINE-24.D adds transport dispatch/write diagnostics:

- missing or invalid transport dispatch wiring;
- response serialization or response-buffer-capacity failure;
- response write-start or write-completion failure;
- reserved close-after-write lifecycle failure if that becomes detectable.

ENGINE-24.E adds transport cancellation/timeout/shutdown diagnostics:

- client disconnect during head/body read uses `SLOPPY_E_HTTP_CONNECTION_CLOSED`;
- header, body, and total request timeouts use `SLOPPY_E_HTTP_REQUEST_TIMEOUT` and return
  `408` when safe;
- write timeout/failure uses `SLOPPY_E_HTTP_WRITE_FAILED`;
- shutdown rejection/cancellation uses `SLOPPY_E_HTTP_SHUTDOWN` through the backend
  lifecycle where a request exists;
- invalid lifecycle transitions remain `SLOPPY_E_APP_LIFECYCLE`.

## Tests

Implemented CTest coverage:

- valid route patterns;
- invalid route patterns;
- static route matching;
- string and integer parameter matching;
- strict trailing slash behavior;
- query string rejection;
- borrowed parameter value lifetime;
- valid request targets including query stripping into `path`;
- Host and multiple header capture;
- non-NUL-terminated request input and binary body capture without C-string assumptions;
- supported method mapping;
- malformed request lines, missing versions, invalid methods/tokens, invalid headers,
  incomplete requests, and empty targets;
- max-header enforcement;
- header name, header value, and total header byte limit enforcement;
- body byte capture and max-body enforcement;
- request target length enforcement;
- parser diagnostics for malformed requests and header limits;
- backend init/start/stop/dispose;
- connection lifecycle cleanup;
- request lifecycle success and cleanup;
- bounded repeated valid request lifecycle smoke with counter release;
- bounded repeated malformed request lifecycle smoke with deterministic diagnostics and
  release;
- repeated parser target/header/body limit failures under backend parser options;
- bounded body-reader success, empty body, arena-owned body bytes, body limits, unsupported
  media, cancellation before body read, cancellation during body read, timeout during body
  read, shutdown during body read, shutdown rejection, active request shutdown cancellation,
  shutdown during response write, and stable shutdown diagnostic names;
- repeated body-limit and unsupported-media body-reader failures;
- malformed request cleanup after parser failure;
- timeout hook cancellation and diagnostic behavior;
- bounded admission/backpressure rejection;
- repeated overload rejection without unbounded request queue growth;
- repeated shutdown rejection/cancellation cleanup smoke;
- route matcher reuse with a parsed request path;
- route table construction, duplicate route rejection, and literal-before-parameter
  precedence;
- query parameter limit enforcement;
- synthetic dispatch no-route failure;
- synthetic dispatch method mismatch failure;
- synthetic dispatch GET/POST/PUT/PATCH/DELETE method routing;
- unsupported request body dispatch failure;
- unsupported content-type, invalid JSON, and body-too-large dispatch failures;
- default non-V8 conformance-style smoke for GET/POST/PUT/PATCH/DELETE, route miss, method
  mismatch, malformed query, malformed JSON, and unsupported media diagnostics;
- transport config validation, init/stop/dispose, localhost ephemeral bind/listen, double
  listen rejection, one accepted TCP connection, bounded max-connection overflow close,
  accepted connection cleanup, and dispose-after-listen cleanup;
- transport read-loop start on accepted connection, complete GET in one chunk, split
  request head, body in the same chunk as the head, body split across chunks, empty body,
  head-too-large rejection, malformed-head rejection, parser header-limit flow-through,
  body-too-large rejection, unsupported transfer encodings, malformed chunked bodies, unsupported media
  rejection, pipelined request-byte rejection, request-ready internal hook observation,
  no-hook ready-request cleanup, cleanup after read/partial-body failure, and cleanup after
  ready request;
- transport dispatch/write tests for request-ready dispatch exactly once, simple GET
  success bytes observed by a real TCP client, route miss 404 mapping, method-not-allowed
  405 mapping, dispatch failure safe 500 mapping, response write completion
  close-after-response cleanup, response buffer lifetime through write callback,
  no dispatch after close, no second request after close, and response buffer capacity
  diagnostics;
- localhost TCP transport smoke/conformance tests for raw client request bytes, GET
  success, sequential two-request keep-alive reuse, `Connection: close`, disabled
  keep-alive config, HTTP/1.0 close policy, idle timeout, max requests, 404 route miss, 405
  method mismatch, POST text body success, malformed input, body-too-large, unsupported
  media type, unsupported transfer encodings, chunked request decoding, chunked streaming
  response framing, response `Content-Length`/managed `Connection` bytes, and cleanup/counter coherence; the same
  transport target also covers unsupported pipelining through the deterministic feed path;
- synthetic dispatch missing plan handler failure before engine entry;
- route parameter match through dispatch and context materialization;
- V8-gated dispatch success returning `sloppy-ok`;
- V8-gated non-GET dispatch, request headers, and JSON body context;
- V8-gated missing JavaScript function and throwing handler failures;
- ENGINE-19.BC CTest conformance aliases:
  `conformance.http.default_dispatch`, `conformance.transport.localhost_mvp`, and
  V8-gated `conformance.v8.http_dispatch_execution`;
- default CLI tests for `sloppy run` help text, missing artifacts, malformed artifacts,
  source-input deferral, and clear V8-disabled failure;
- V8-gated `sloppy run --once` tests for hello, route miss, unsupported method,
  request context, and invalid result descriptors when the build is configured with V8.

EPIC-20 adds manual benchmarks for route matching and complete-buffer request-head parsing.
The request-head benchmark is a parser microbenchmark only. It is not an HTTP server
throughput benchmark and does not involve sockets, response writing, middleware, or public
TypeScript APIs.

EPIC-23 and ENGINE-22.A tests add native response writer exact-byte coverage, binary body
output coverage, capacity-failure output reset coverage, query parser coverage for empty,
repeated, decoded, and malformed query strings, and compiler/example coverage for request
context shape.

Future tests:

- route table ambiguity tests;
- broader HTTP server/socket integration tests;
- future keep-alive stress/conformance tests for higher-volume sequential reuse, socket
  backpressure, and richer shutdown drain timing;
- #446 future keep-alive/streaming stress tests for higher-volume reuse, richer write
  backpressure timing, and shutdown/client-disconnect stress beyond the current smoke;
- fuzz target for route patterns;
- broader response writer behavior beyond the current dev-only MVP.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/execution-model.md`;
- `docs/testing-strategy.md`;
- `docs/dependencies.md`;
- `docs/build-and-distribution.md`;
- `docs/concurrency.md`;
- `docs/app-plan.md`;
- `docs/modules/plan/README.md`;
- `docs/modules/engine-v8/README.md`.

## Open Questions

- Exact timing for starting #433 HTTP-25 implementation.
- Exact future production route table shape.
