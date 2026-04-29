# HTTP Module

## Status

Partially implemented.

TASK 10.A adds a native route pattern parser and matcher foundation. TASK 10.B adds the
first llhttp/libuv dependency integration skeleton and a complete-buffer HTTP/1 request-head
parser. TASK 10.C adds a synthetic in-memory GET dispatch helper that maps a parsed request
head and manual route binding to a numeric Sloppy Plan handler ID, then calls the existing
runtime-contract/engine boundary. EPIC-22 adds a dev-only `sloppy run` path that uses libuv
to accept one complete request head per connection, dispatches GET routes from EPIC-21
artifact metadata, writes a tiny text/JSON-compatible response, and closes the connection.
There is still no production HTTP server, body parsing, streaming parser API, middleware,
request context, public TypeScript `app.run`, or full response writer.

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
- run a minimal libuv loop init/close smoke to prove dependency linkage;
- dispatch one parsed in-memory GET request head through a manual route binding table to a
  Sloppy Plan handler ID;
- invoke the matched handler through the existing runtime-contract helper when an engine is
  available;
- return the existing simple `SlEngineResult` text shape from synthetic dispatch tests.
- dev-only `sloppy run --artifacts <dir>` server over libuv;
- deterministic `sloppy run --artifacts <dir> --once METHOD TARGET` synthetic dispatch;
- minimal HTTP responses with status line, `Content-Type`, `Content-Length`, body bytes,
  and connection close.

Future scope:

- streaming HTTP parser state;
- HTTP body parsing;
- request lifecycle;
- production method dispatch and server hardening;
- route table/trie or other optimized dispatch structure;
- HTTP response conversion and writing.

EPIC-14 module routes are bootstrap `app.mapGet` registrations only. They do not connect
module routes to the native HTTP parser, synthetic dispatch helper, route params, or
response writer.

## Non-goals

- No production HTTP server, body parsing, streaming parser, middleware, compiler work,
  public TypeScript API, or app host behavior in TASK 10.B, TASK 10.C, or EPIC-22.
- No sockets, request/response objects, middleware, route groups, route table, trie,
  precedence engine, public TypeScript API, `app.mapGet`, validation, OpenAPI, V8, or
  compiler extraction in TASK 10.A or TASK 10.B.
- No route params passed to JavaScript, request context, route groups, route precedence,
  middleware, `Results.*`, response formatting, or public TypeScript API in TASK 10.C.
- EPIC-13 route groups exist only in `stdlib/sloppy/app.js` as in-memory bootstrap
  metadata. They do not alter the native route parser, matcher, synthetic dispatch helper,
  HTTP route table, or request context.

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
- `sl_http_parse_request_head`;
- `sl_http_libuv_smoke`.

`include/sloppy/http_dispatch.h` exposes the synthetic TASK 10.C dispatch helper:

- `SlHttpRouteBinding`;
- `SlHttpDispatchTable`;
- `sl_http_dispatch_request_head`.

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

The HTTP parser accepts a complete HTTP/1.x request head in one `SlBytes` buffer. It does
not expose streaming state. It rejects malformed input, incomplete input, missing HTTP
versions, unsupported methods, empty/non-path request targets, and header counts above
`max_headers`. When parse options are omitted, `SL_HTTP_DEFAULT_MAX_HEADERS` is used. A
zero header limit allows requests with no headers and rejects the first parsed header.

Supported method mapping is intentionally small: GET, POST, PUT, DELETE, PATCH, OPTIONS,
and HEAD. Other llhttp methods fail as unsupported for this skeleton.

`raw_target` stores the request target exactly as reported by llhttp. `path` stores the
portion before `?`. TASK 10.B only accepts origin-form path targets that start with `/`;
asterisk-form and absolute-form targets are rejected before returning success. Query
parsing, percent decoding, URL normalization, and host validation are deferred.

The dispatch helper accepts an already parsed `SlHttpRequestHead`, a borrowed route binding
table, a parsed `SlPlan`, and an `SlEngine`. TASK 10.C supports only GET request dispatch.
Route bindings borrow parsed `SlRoutePattern` values and carry a numeric `SlHandlerId`.
The helper matches `request.path`, validates the handler ID exists in the plan before
entering the engine, then calls `sl_runtime_contract_call_handler`. Route parameters may
participate in matching but are not passed to JavaScript.

`sloppy run` builds that manual binding table from the compiler-emitted `routes` metadata
section. Its response writer is deliberately local to the CLI MVP: `200` for handler text,
`404` for route misses, `405` for unsupported methods, and safe `500` text for malformed
requests or handler/runtime failures.

## Ownership/Lifetime Rules

Parsed route patterns copy source text, static segment text, and parameter names into the
caller-provided `SlArena`. Parsed pattern views remain valid until that arena is reset or
its backing storage ends.

Successful route match arrays are allocated from the caller-provided match arena. Failed
matches return no parameter array. Captured parameter names point into the parsed pattern
arena. Captured parameter values are borrowed slices of the matched path input and are valid
only while that path storage remains valid.

HTTP request-head data returned by `sl_http_parse_request_head` is arena-owned. The caller
must keep the arena backing storage alive for the desired request-head lifetime. The parser
does not return pointers into llhttp temporary state. It also does not retain pointers to
the input buffer after success.

HTTP dispatch tables borrow route bindings, parsed route patterns, plans, and engine
handles for the duration of the call only. `sl_http_dispatch_request_head` does not retain
request, route, plan, engine, or route-parameter storage. Successful V8 text results follow
the existing engine/runtime-contract rule: text is copied into the caller-provided arena.

Request data must have documented ownership and may not outlive its scope unsafely once HTTP
request handling grows beyond this skeleton.

## Invariants

Route parsing is bounded and cannot rely on unbounded recursion. TASK 10.A uses fixed
maximums for one pattern: `SL_ROUTE_MAX_SEGMENTS` and `SL_ROUTE_MAX_PARAMS`.

The route foundation is pure core C: no OS APIs, V8 types, llhttp, libuv, sockets, or heap
allocation are introduced by route parsing/matching.

The HTTP parser skeleton depends on llhttp and libuv through vcpkg/CMake. The parser itself
uses no OS-specific headers or direct OS APIs. libuv use is limited to a local loop
init/close smoke helper and does not integrate with `SlLoop`.

The dispatch helper is pure core C over existing parser, route, plan, runtime-contract, and
engine boundaries. It does not include V8 headers, expose V8 handles, call OS APIs, create
sockets, drive libuv, allocate outside the caller arena, or build a production router
object. The dev-only socket loop lives in the CLI executable and uses libuv without OS
headers or platform-specific calls.

## Diagnostics

Implemented parser diagnostics are intentionally small:

- malformed/invalid route pattern;
- duplicate route parameter name.

TASK 10.B adds small HTTP parser diagnostics:

- invalid/malformed/incomplete HTTP request;
- HTTP header count limit exceeded.

TASK 10.C adds small synthetic dispatch diagnostics:

- unsupported non-GET dispatch method;
- no matching route;
- matched route references a missing plan handler;
- missing/non-callable/throwing JavaScript functions through the existing engine diagnostic
  path.

Future diagnostics:

- duplicate routes;
- ambiguous routes;
- request conversion errors;
- route conflict/source-span diagnostics.
- route group/source metadata diagnostics once compiler extraction and plan route sections
  exist.

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
- supported method mapping;
- malformed request lines, missing versions, invalid methods/tokens, invalid headers,
  incomplete requests, and empty targets;
- max-header enforcement;
- parser diagnostics for malformed requests and header limits;
- route matcher reuse with a parsed request path;
- libuv init/close smoke without network I/O;
- synthetic dispatch no-route failure;
- synthetic dispatch non-GET failure;
- synthetic dispatch missing plan handler failure before engine entry;
- route parameter match through dispatch without passing params to JavaScript;
- V8-gated dispatch success returning `sloppy-ok`;
- V8-gated missing JavaScript function and throwing handler failures.
- default CLI tests for `sloppy run` help text, missing artifacts, malformed artifacts,
  source-input deferral, and clear V8-disabled failure;
- V8-gated `sloppy run --once` tests for hello, route miss, and unsupported method when the
  build is configured with V8.

EPIC-20 adds manual benchmarks for route matching and complete-buffer request-head parsing.
The request-head benchmark is a parser microbenchmark only. It is not an HTTP server
throughput benchmark and does not involve sockets, response writing, request bodies,
middleware, or public TypeScript APIs.

Future tests:

- route table and ambiguity tests;
- broader HTTP server/socket integration tests;
- fuzz target for route patterns;
- full response writer tests;
- route params in handler context tests.

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

- Exact HTTP server/socket integration timing.
- Exact future production route table shape.
