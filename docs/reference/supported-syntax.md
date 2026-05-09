# Supported Syntax Reference

This page describes the current Sloppy source subset accepted by `sloppyc`.

## File Extensions

Accepted entry/module extensions:

- `.js`
- `.mjs`
- `.ts`

Rejected examples include `.jsx`, `.tsx`, `.cjs`, `.mts`, `.cts`.

## Import Specifiers

### Supported module specifiers

- `"sloppy"`
- `"sloppy/data"`
- `"sloppy/providers/sqlite"`
- `"sloppy/providers/postgres"`
- `"sloppy/providers/sqlserver"`
- `"sloppy/fs"`
- `"sloppy/time"`
- `"sloppy/crypto"`
- `"sloppy/codec"`
- `"sloppy/net"`
- `"sloppy/os"`
- `"sloppy/workers"`
- relative imports (`./...`, `../...`) that stay within source root

### Unsupported import behavior

- dynamic import (`import(...)`) is rejected
- non-listed bare specifiers are rejected
- import aliasing for supported names is rejected in compiler subset
- default/namespace import forms for these surfaces are rejected

## Route Registration Syntax

Supported route methods:

- `mapGet` / `get`
- `mapPost` / `post`
- `mapPut` / `put`
- `mapPatch` / `patch`
- `mapDelete` / `delete`

Unsupported route method forms:

- computed route methods (for example `app[method](...)`)
- unsupported verb helpers (`head`, `options`, unknown `map*`)

## Route Pattern Grammar

Allowed:

- `/`
- static segments: `/users`
- parameter segments: `{id}`, `{id:str}`, `{id:int}`
- framework `:name` segments normalize to `{name}`

Rejected:

- pattern not starting with `/`
- trailing `/` (except root `/`)
- duplicate slash `//`
- stray braces in static segments
- unknown parameter kind (only `str` and `int`)

## Handler Shape (non-typed route handlers)

Accepted baseline:

- zero parameters, or one simple identifier context parameter
- direct supported `Results.*` return shape

Rejected examples:

- destructuring/default/rest parameters
- closed-over unsupported values in `Results.*` payloads
- unsupported async handler bodies (outside supported direct return extraction)

## TypeScript-Typed Handler Subset

Typed parameter wrappers currently supported:

- `Body<T>`
- `Query<T>`
- `Route<T>`
- `Header<"name", T>`
- `Service<T>`
- `Config<T>`
- provider markers `Postgres<"...">`, `Sqlite<"...">`, `SqlServer<"...">`
- context markers `RequestContext`, `SlopRequest`, `SlopResponse`, `CancellationSignal`, `Deadline`

Key typed limits:

- provider generic name must be string literal
- one body binding per handler
- route parameter names must be bound by handler signature
- unresolved schema/body types are rejected
