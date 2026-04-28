# HTTP Module

## Status

Partially implemented.

TASK 10.A adds a native route pattern parser and matcher foundation. There is still no HTTP
server, request parser, route table, method dispatch, public TypeScript API, or
`app.mapGet` runtime behavior.

## Purpose

Provide native HTTP parsing, routing, and dispatch once prerequisite runtime contract work
exists.

## Scope

Implemented now:

- parse one native route pattern into arena-owned segments;
- match one path against one parsed pattern;
- capture route parameters in deterministic order.

Future scope:

- HTTP integration;
- request lifecycle;
- method dispatch;
- route table/trie or other optimized dispatch structure;
- handler dispatch.

## Non-goals

- No HTTP parser or server in TASK 10.A.
- No llhttp or libuv integration in TASK 10.A.
- No sockets, request/response objects, middleware, route groups, route table, trie,
  precedence engine, public TypeScript API, `app.mapGet`, validation, OpenAPI, V8, or
  compiler extraction in TASK 10.A.

## Public/Internal API

`include/sloppy/route.h` exposes the internal native route foundation:

- `SlRoutePattern`;
- `SlRouteSegment`;
- `SlRouteParam`;
- `SlRouteMatch`;
- `sl_route_pattern_parse`;
- `sl_route_pattern_match`.

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

## Ownership/Lifetime Rules

Parsed route patterns copy source text, static segment text, and parameter names into the
caller-provided `SlArena`. Parsed pattern views remain valid until that arena is reset or
its backing storage ends.

Route match arrays are allocated from the caller-provided match arena. Captured parameter
names point into the parsed pattern arena. Captured parameter values are borrowed slices of
the matched path input and are valid only while that path storage remains valid.

Request data must have documented ownership and may not outlive its scope unsafely once HTTP
request handling exists.

## Invariants

Route parsing is bounded and cannot rely on unbounded recursion. TASK 10.A uses fixed
maximums for one pattern: `SL_ROUTE_MAX_SEGMENTS` and `SL_ROUTE_MAX_PARAMS`.

The route foundation is pure core C: no OS APIs, V8 types, llhttp, libuv, sockets, or heap
allocation are introduced.

## Diagnostics

Implemented parser diagnostics are intentionally small:

- malformed/invalid route pattern;
- duplicate route parameter name.

Future diagnostics:

- duplicate routes;
- ambiguous routes;
- request conversion errors;
- route conflict/source-span diagnostics.

## Tests

Implemented CTest coverage:

- valid route patterns;
- invalid route patterns;
- static route matching;
- string and integer parameter matching;
- strict trailing slash behavior;
- query string rejection;
- borrowed parameter value lifetime.

Future tests:

- route table and ambiguity tests;
- HTTP integration tests;
- fuzz target for route patterns.

## Source Docs

- `docs/developer-ergonomics.md`;
- `docs/execution-model.md`;
- `docs/testing-strategy.md`.

## Open Questions

- Exact llhttp/libuv introduction timing.
