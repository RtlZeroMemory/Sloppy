# Routing Reference

Routing behavior is split across two surfaces:

- bootstrap app/router registration in `stdlib/sloppy/internal/routes.js`
- compiler route extraction and Plan route validation in `compiler/src/sloppyc.rs`

## Registration Methods

Current route registration methods:

- `mapGet`, `mapPost`, `mapPut`, `mapPatch`, `mapDelete`
- aliases: `get`, `post`, `put`, `patch`, `delete`

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

- route registration must be top-level
- route method calls must target extracted app/group variables
- pattern must be a string literal
- handler must be an inline function/arrow in supported shapes
- dynamic route strings, computed methods, loop/conditional registration are rejected

## Runtime Dispatch Notes

- `sloppy run --once` method parser accepts `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`, and `HEAD`.
- Route availability still depends on emitted Plan metadata.

## Limits

- No Node-style router behavior contract.
- No middleware/filter pipeline contract in this layer.
- No package-manager route plugin model.
