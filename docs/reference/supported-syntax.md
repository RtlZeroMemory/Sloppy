# Compiler Supported Syntax Reference

This page is compiler-specific (`sloppyc`) and lists enforced source-shape rules.

## Input and Parse

- Entry and module files must use `.js`, `.mjs`, or `.ts`.
- Parse failures return `SLOPPYC_E_PARSE`.
- Unsupported extension returns `SLOPPYC_E_UNSUPPORTED_INPUT`.

## Required Sloppy Imports

Compiler input must import:

- `Sloppy` from `"sloppy"`
- `Results` from `"sloppy"`

These imports must be named and unaliased in current compiler subset.

## Supported Import Modules

Compiler-recognized modules:

- `"sloppy"`
- `"sloppy/data"` (`sql`)
- `"sloppy/providers/sqlite"` (`sqlite`, `Sqlite`)
- `"sloppy/providers/postgres"` (`Postgres`)
- `"sloppy/providers/sqlserver"` (`SqlServer`)
- `"sloppy/fs"`, `"sloppy/time"`, `"sloppy/crypto"`, `"sloppy/codec"`, `"sloppy/net"`, `"sloppy/os"`, `"sloppy/workers"`
- relative imports constrained to source root

Unsupported specifiers/import names fail with:

- `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER`
- `SLOPPYC_E_UNSUPPORTED_IMPORT`

Dynamic import fails with `SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT`.

`RequestId` and `RequestLogging` are supported for static middleware defaults.
`Testing` is a framework test helper; compiler input rejects it with
`SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT`.

## Route Extraction Rules

Route declarations must be top-level statements on app/group receivers.

Route methods:

- GET, POST, PUT, PATCH, DELETE (`map*` and plain verb forms)

Supported route metadata:

- `app.get("/path", handler).withName("Name")`
- `app.get("/path", { name: "Name", tags: ["tag"] }, handler)`
- `app.group("/prefix").withTags("tag")`
- `app.mapHealthChecks(...)` with literal paths and literal check metadata
- top-level `app.use(fn)` and `group.use(fn)` middleware with inline or
  top-level static functions;
- top-level `app.useCors({...})` with literal policy objects;
- top-level `app.use(RequestId.defaults({...}))` and
  `app.use(RequestLogging.defaults({...}))` with static options;
- top-level `app.mapController(...)` / `app.controller(...)` with a top-level
  plain controller class and literal mapper calls;

Route metadata options must be literal objects. `name` must be a non-empty
string literal. `tags` must be an array of non-empty string literals.
Health options must be omitted, a string literal aggregate path, or a literal
object with `path`, `livenessPath`, `readinessPath`, and `checks`. Check
objects must use literal names, boolean `liveness`/`readiness` flags, and
inline check functions.

Common route failures:

- `SLOPPYC_E_UNSUPPORTED_ROUTE_SHAPE`
- `SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD`
- `SLOPPYC_E_UNSUPPORTED_HTTP_METHOD`
- `SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_NAME`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS`
- `SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS`

Health checks must be inline functions that do not capture module-level locals.
Captured values fail with `SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS`.

## Framework Static Subset

These app-host features are emitted when they stay inside the static subset:

- middleware functions must be inline or top-level functions with supported
  captures;
- CORS policies must be literal objects;
- RequestId options must be static and cannot use generator callbacks;
- RequestLogging options must be static booleans;
- controller mappings must target a top-level plain class and literal mapper
  route calls.

Unsupported dynamic shapes fail with `SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`,
`SLOPPYC_E_UNSUPPORTED_CORS`, `SLOPPYC_E_UNSUPPORTED_REQUEST_ID`,
`SLOPPYC_E_UNSUPPORTED_REQUEST_LOGGING`, or
`SLOPPYC_E_UNSUPPORTED_CONTROLLER` instead of accepting a partial Plan.

## Services And Config

Literal service registrations are supported on both `app.services` and
`builder.services`:

- `addSingleton("Token", () => value)`
- `addScoped("Token", () => value)`
- `addTransient("Token", () => value)`

Factories must be inline functions that do not capture unsupported identifiers.

`Config<"KEY">` typed parameters read the environment value first. When the
source also contains a literal `app.config.getString("KEY", "default")` default,
the generated wrapper uses that default if the environment value is absent.

## Pattern Rules

Compiler-enforced route grammar:

- `/`
- static segments without braces
- `{name}`, `{name:str}`, `{name:int}`

Normalization:

- framework `:name` is normalized to `{name}` before validation

## Handler Diagnostics

Representative handler diagnostics:

- `SLOPPYC_E_UNSUPPORTED_HANDLER`
- `SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`
- `SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER`
- `SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE`
- `SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY`

## Typed Handler Binding Diagnostics

Representative typed-binding diagnostics:

- `SLOPPYC_E_UNSUPPORTED_HEADER_BINDING`
- `SLOPPYC_E_DYNAMIC_PROVIDER_NAME`
- `SLOPPYC_E_UNKNOWN_INJECTION_MARKER`
- `SLOPPYC_E_UNRESOLVED_TYPE`
- `SLOPPYC_E_MULTIPLE_BODY_BINDINGS`
- `SLOPPYC_E_ROUTE_BINDING_MISMATCH`
- `SLOPPYC_E_UNBOUND_ROUTE_PARAMETER`
- `SLOPPYC_E_AMBIGUOUS_BINDING`

## Provider Bridge Limitation

Compiler metadata recognizes sqlite/postgres/sqlserver providers. Framework v2
typed provider parameters emit generated wrappers for all three provider kinds,
with PostgreSQL and SQL Server execution gated on provider config, active bridge
support, and live service dependencies.

Generated static provider handles are narrower: `app.provider("sqlite:main")`
is the current executable static-handle path. Static non-SQLite handles such as
`app.provider("postgres:main")` are rejected with
`SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
