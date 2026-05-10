# Compiler Supported Syntax Reference

This page is compiler-specific (`sloppyc`) and lists enforced source-shape rules.

## Input and Parse

- Entry and module files must use `.js`, `.mjs`, or `.ts`.
- Parse failures return `SLOPPYC_E_PARSE`.
- Unsupported extension returns `SLOPPYC_E_UNSUPPORTED_INPUT`.

## Sloppy Imports

Web compiler entry input must import:

- `Sloppy` from `"sloppy"`

Files that contain route handlers calling `Results.*` must also import
`Results` from `"sloppy"` in that same file. A thin entry file that only creates
the app and registers function modules can import `Sloppy` alone; child route
modules import `Results` when their handlers use it.

These imports must be named and unaliased in the current compiler subset.

## Supported Import Modules

Compiler-recognized modules:

- `"sloppy"`
- `"sloppy/data"` (`sql`)
- `"sloppy/providers/sqlite"` (`sqlite`, `Sqlite`)
- `"sloppy/providers/postgres"` (`Postgres`)
- `"sloppy/providers/sqlserver"` (`SqlServer`)
- `"sloppy/fs"`, `"sloppy/time"`, `"sloppy/crypto"`, `"sloppy/codec"`, `"sloppy/net"`, `"sloppy/os"`, `"sloppy/workers"`
- relative imports constrained to source root
- installed pure-JavaScript package imports in Program Mode
- supported Node shim specifiers such as `node:path` in Program Mode

Unsupported specifiers/import names fail with:

- `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER`
- `SLOPPYC_E_UNSUPPORTED_IMPORT`
- `SLOPPYC_E_PACKAGE_NOT_FOUND`
- `SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED`
- `SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED`
- `SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN`

Web app extraction still rejects dynamic import with
`SLOPPYC_E_UNSUPPORTED_DYNAMIC_IMPORT`. Program Mode supports string-literal
dynamic imports and computed dynamic imports over explicit `moduleInclude`
graphs.

`RequestId` and `RequestLogging` are supported for static middleware defaults.
`Testing` is a framework test helper; compiler input rejects it with
`SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT`.

## Program Mode Source

Program Mode supports route-free source files that use static ESM imports and
exports. Oxc parses the source and strips supported TypeScript syntax before
Sloppy rewrites supported module syntax into the generated artifact bundle.

Supported entrypoint shapes are:

```ts
console.log("hello");

export async function main() {
  console.log("hello");
}

export default async function main() {
  console.log("hello");
}
```

If both named `main` and default function exports exist, named `main` wins.
Non-function default exports are ignored as entrypoints after their module
top-level code has executed.

`main` and default function entrypoints receive `(args, ctx)`. `args` contains
the strings passed after `--` to `sloppy run`. `ctx` is the Program context:

```ts
{
  kind: "program",
  args: string[],
  cwd: string,
  environment: string,
  plan: {
    kind: "program",
    metadataCompleteness: "opaque"
  }
}
```

Program Mode installs a Sloppy-owned `console` while the entry module runs.
`log`, `info`, and `debug` write to stdout; `warn` and `error` write to
stderr. Returning an integer from `0` through `255` sets the process exit code.
Throwing, rejecting, or returning an out-of-range exit code exits non-zero with
a diagnostic.

Program Mode accepts static relative imports, installed pure-JavaScript package
imports, CommonJS/JSON modules, the documented Sloppy stdlib subpaths, and the
explicit Node compatibility registry. Type-only imports do not emit runtime
stdlib features. String-literal dynamic imports are resolved at build time.
Computed dynamic imports are limited to modules sealed into the graph with
`moduleInclude`.

Remote imports, native Node addons, unsupported Node builtins, unsupported
package export shapes, and Sloppy provider imports are rejected with
diagnostics.

Program Mode does not support re-export declarations yet. Use an import plus a
local export instead of `export { value } from "./dep"` or `export * from
"./dep"`.

### Package and compatibility diagnostics

Representative dependency diagnostics:

- `SLOPPYC_E_PACKAGE_NOT_FOUND`
- `SLOPPYC_E_PACKAGE_EXPORT_UNSUPPORTED`
- `SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED`
- `SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN`

Node compatibility shims are modules, not full Node globals. Import
`node:process` or `node:buffer` explicitly when the partial shim is supported.

### Stdlib subpath imports

The compiler accepts named, unaliased imports from each stdlib subpath listed
below. Each subpath emits the matching `stdlib.*` runtime feature into the
Plan when imported. Default imports and import aliases are rejected with
`SLOPPYC_E_UNSUPPORTED_IMPORT`; unsupported names fail with the same code.

| Subpath | Supported names | Plan feature |
| --- | --- | --- |
| `sloppy/fs` | `File`, `Directory`, `FileHandle`, `FileWatcher`, `Path` | `stdlib.fs` |
| `sloppy/net` | `HttpClient`, `TcpClient`, `TcpListener`, `TcpConnection`, `LocalEndpoint`, `UnixSocket`, `NamedPipe`, `NetworkAddress`, `SloppyNetError` | `stdlib.net` (`HttpClient` emits `stdlib.httpclient`) |
| `sloppy/os` | `System`, `Environment`, `Process`, `ProcessHandle`, `Signals`, `OsError` | `stdlib.os` |
| `sloppy/time` | `Time`, `Deadline`, `CancellationController`, `TimeoutError`, `CancelledError`, `InvalidDeadlineError`, `TimerDisposedError` | `stdlib.time` |
| `sloppy/crypto` | `Random`, `Hash`, `Hmac`, `Password`, `ConstantTime`, `Secret`, `NonCryptoHash` | `stdlib.crypto` |
| `sloppy/codec` | `Base64`, `Base64Url`, `Hex`, `Text`, `Binary`, `Compression`, `Checksums` | `stdlib.codec` |
| `sloppy/workers` | `BackgroundService`, `WorkQueue`, `WorkerPool`, `Worker`, `WorkerCancellationController`, `WorkerCancellationSignal`, `SloppyWorkerError` | `stdlib.workers` |

Examples:

```ts
import { File, Directory } from "sloppy/fs";
import { HttpClient } from "sloppy/net";
import { System, Environment, Process, Signals } from "sloppy/os";
import { Time, Deadline, CancellationController } from "sloppy/time";
import { Random, Hash, Hmac, Secret } from "sloppy/crypto";
import { Base64, Hex, Text, Binary } from "sloppy/codec";
import { BackgroundService, WorkQueue } from "sloppy/workers";
```

## Route Extraction Rules

Static route declarations are strongest when they are top-level statements on
app/group receivers. Sloppy does not require every route to be statically
understood. If the compiler can emit runnable JavaScript, the app can run.
Static source gives stronger Plan metadata; dynamic source produces partial
metadata and findings.

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
- `SLOPPYC_E_UNSUPPORTED_HTTP_METHOD`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_PATTERN`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_NAME`
- `SLOPPYC_E_UNSUPPORTED_ROUTE_OPTIONS`
- `SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS`

Dynamic route uncertainty is reported as Plan findings instead of fatal
metadata extraction diagnostics:

- `SLOPPYC_W_DYNAMIC_ROUTE`
- `SLOPPYC_W_PARTIAL_ROUTE_METADATA`
- `SLOPPYC_W_DYNAMIC_RESPONSE_METADATA`

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

Unsupported dynamic framework features fail with `SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`,
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

Compiler metadata recognizes sqlite/postgres/sqlserver providers. Typed provider
parameters emit generated wrappers for all three provider kinds, with PostgreSQL
and SQL Server execution gated on provider config, active bridge support, and
live service dependencies.

Generated static provider handles are narrower: `app.provider("sqlite:main")`
is the current executable static-handle path. Static non-SQLite handles such as
`app.provider("postgres:main")` are rejected with
`SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
