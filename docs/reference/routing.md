# Routing Reference

Routing behavior is split across two surfaces:

- bootstrap app/router registration in `stdlib/sloppy/internal/routes.js`
- compiler route extraction and Plan route validation in `compiler/src/sloppyc.rs`

## Registration Methods

Current route registration methods:

- `mapGet`, `mapPost`, `mapPut`, `mapPatch`, `mapDelete`
- aliases: `get`, `post`, `put`, `patch`, `delete`
- `mapHealthChecks` for the bootstrap aggregate health, liveness, and readiness
  route set

Unsupported direct methods in compiler extraction are rejected (for example `head` and `options` registrations).

## Route Pattern Rules

Plan-supported route patterns follow these rules:

- `/` is valid.
- non-root routes must start with `/`.
- no trailing slash (except root).
- no `//`.
- each segment is either:
  - literal text without `{` or `}`
  - `{name}` or `{name:str}` or `{name:int}`

Compiler normalization also accepts framework `/:name` segments and converts them to `{name}` in Plan metadata.

## Group Rules

- `mapGroup(prefix)` / `group(prefix)` require a prefix that starts with `/`.
- group prefixes are normalized (for example `/users/` -> `/users`).
- nested groups compose parent + child patterns.
- group metadata supports `withTags(...tags)` and `withName(name)`.
- bootstrap groups support `use(fn)` for group-local middleware functions.
- compiler extraction emits static `group.use(fn)` middleware when the function
  shape is supported.

## Route Metadata

Route registration returns an endpoint builder with:

- `withName(name)`

Validation:

- endpoint names must be non-empty strings
- duplicate `method + pattern` is rejected
- duplicate route names are rejected in compiler extraction

## Handler Shape Requirements

Bootstrap registration requires handler functions.

Compiler extraction currently enforces:

- statically extracted route method calls must target extracted app/group variables
- literal patterns and supported handler shapes produce complete route metadata
- computed route strings, loops, conditionals, and helper registration can run
  when the generated JavaScript stays inside Sloppy's runtime boundary
- dynamic route shapes produce partial/dynamic Plan metadata and findings

## Runtime Dispatch Notes

- `sloppy run --once` method parser accepts `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`, and `HEAD`.
- Incoming `HEAD` matches the corresponding `GET` route and suppresses response
  body bytes at the transport boundary.
- Plan-backed `405 Method Not Allowed` responses include an `Allow` header when
  the route table can match the request path. `GET` routes also advertise `HEAD`.
- Static route-table availability depends on emitted complete Plan metadata.
  Dynamic source-input routes execute through the generated JavaScript runtime
  path when V8 is enabled.
- CORS-enabled routes synthesize `OPTIONS` preflight route entries in the
  app-host route table and in compiler-emitted Plan metadata for static CORS
  policies.

## Limits

- Sloppy routing currently follows Sloppy app/group registration and supported
  route patterns. Node-style router APIs are not part of the current surface.
- Package-manager route plugins are outside the current pre-alpha route table.
