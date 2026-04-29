# HTTP Module

## Status

Partially implemented.

TASK 10.A adds a native route pattern parser and matcher foundation. TASK 10.B adds the
first llhttp/libuv dependency integration skeleton and a complete-buffer HTTP/1 request-head
parser. There is still no HTTP server, socket I/O, body parsing, streaming parser API,
route table, method dispatch, public TypeScript API, or `app.mapGet` runtime behavior.

## Purpose

Provide native HTTP parsing, routing, and dispatch once prerequisite runtime contract work
exists.

## Scope

Implemented now:

- parse one native route pattern into arena-owned segments;
- match one path against one parsed pattern;
- capture route parameters in deterministic order.
- parse one complete in-memory HTTP/1 request head with llhttp;
- map supported request methods into `SlHttpMethod`;
- copy request target, path, header names, and header values into a caller-provided arena;
- run a minimal libuv loop init/close smoke to prove dependency linkage.

Future scope:

- TCP accept/read/write integration;
- streaming HTTP parser state;
- HTTP body parsing;
- request lifecycle;
- method dispatch;
- route table/trie or other optimized dispatch structure;
- handler dispatch.

## Non-goals

- No HTTP server, sockets, response writer, body parsing, streaming parser, route dispatch,
  middleware, V8/compiler work, public TypeScript API, or app host behavior in TASK 10.B.
- No sockets, request/response objects, middleware, route groups, route table, trie,
  precedence engine, public TypeScript API, `app.mapGet`, validation, OpenAPI, V8, or
  compiler extraction in TASK 10.A or TASK 10.B.

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

## Diagnostics

Implemented parser diagnostics are intentionally small:

- malformed/invalid route pattern;
- duplicate route parameter name.

Future diagnostics:

- duplicate routes;
- ambiguous routes;
- request conversion errors;
- route conflict/source-span diagnostics.

TASK 10.B adds small HTTP parser diagnostics:

- invalid/malformed/incomplete HTTP request;
- HTTP header count limit exceeded.

## Tests

Implemented CTest coverage:

- valid route patterns;
- invalid route patterns;
- static route matching;
- string and integer parameter matching;
- strict trailing slash behavior;
- query string rejection;
- borrowed parameter value lifetime.
- valid request targets including query stripping into `path`;
- Host and multiple header capture;
- supported method mapping;
- malformed request lines, missing versions, invalid methods/tokens, invalid headers,
  incomplete requests, and empty targets;
- max-header enforcement;
- parser diagnostics for malformed requests and header limits;
- route matcher reuse with a parsed request path;
- libuv init/close smoke without network I/O.

Future tests:

- route table and ambiguity tests;
- HTTP server/socket integration tests;
- fuzz target for route patterns.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/execution-model.md`;
- `docs/testing-strategy.md`.
- `docs/dependencies.md`;
- `docs/build-and-distribution.md`;
- `docs/concurrency.md`.

## Open Questions

- Exact HTTP server/socket integration timing.
- Exact route dispatch/table shape for TASK 10.C.
