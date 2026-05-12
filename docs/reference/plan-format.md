# Application Plan

The Sloppy Plan is the compiler/runtime contract. It is deterministic metadata consumed by
native validation, CLI inspection, runtime feature activation, and V8 artifact execution.

## Purpose

The Plan lets Sloppy validate application shape before runtime execution:

- route metadata and handler IDs;
- generated artifact names and hashes;
- required runtime features;
- provider and capability metadata;
- dependency graph and Node compatibility metadata where dependencies are bundled;
- typed native FFI metadata;
- compiler/source metadata for diagnostics;
- configuration metadata where the compiler emits it.

The Plan is not a package manifest, npm metadata format, Node runtime layer, or public
extension system.

## Current Runtime Contract

Plan v1 alpha remains the current runtime contract. Supported artifacts contain
`app.plan.json`, generated `app.js`, and source-map metadata where available. Builds with
dependency graph content also emit `deps.graph.json`. The runtime validates
schema version, route/handler/provider/capability structure, artifact hashes,
runtime target support, FFI metadata, and required features before entering V8.

Handler IDs are compiler-owned numeric IDs. Current executable V8 dispatch uses generated
artifact registration and registered handler dispatch; direct numeric handler-call ABI
entry remains unsupported for the noop engine.

`kind` records the source kind. Missing `kind` is treated as `web` for older
Plans. `kind: "program"` records a route-free Program Mode artifact.

## Routes And Handlers

The compiler can emit route metadata for the supported source subset:

- literal route patterns;
- supported HTTP methods;
- optional route names;
- optional route tags from route metadata and group tags;
- optional health metadata for compiler-extracted health endpoints;
- source metadata;
- direct handler IDs;
- simple request context usage in supported handlers.
- Compiler-inferred metadata for typed handlers, including route/body/query/header
  bindings, context bindings, provider/queue injection requirements, schema definitions,
  semantic validation/redaction metadata, and visible `Results.*` response metadata.
- Per-route `jsonRequest` and `jsonResponse` metadata that records native,
  generic, or fallback JSON request/response handling.
- `routeDispatch` metadata for the native in-memory dispatch table generated
  from Plan routes at runtime.

The native runtime validates route metadata, builds a deterministic route table, and
dispatches supported requests through V8 when the runtime is V8-enabled. For typed-handler
metadata, the native dispatcher also consumes Plan-backed route/query/header scalar bindings and
  schema-backed JSON body metadata to fail invalid requests with a safe `400`
  `application/problem+json` response before calling the handler. Route tags
  and health metadata are optional compiler/CLI metadata; native dispatch does
  not need them to match a request. Static middleware, CORS, request ID,
  request logging, and controller metadata are represented where the compiler
  can lower them. Dynamic route registration is allowed when the transformed
  JavaScript stays inside Sloppy's runtime boundary; the Plan records
  `dynamicRoutes` and warning findings for route pieces the compiler cannot
  fully describe. Arbitrary imports, custom validators, and full TypeScript
  semantics remain outside the current source subset.

Dynamic route metadata is intentionally not complete OpenAPI metadata. Known
static routes stay in `routes[]` with `completeness.status: "complete"` when
all route metadata is inferred. Unknown dynamic registrations appear in
`dynamicRoutes[]`, and app-level metadata records route completeness counts.
OpenAPI emitters include representable known routes and mark the document as a
partial Plan-supported subset.

## Route Dispatch

Web Plans emit a top-level `routeDispatch` object:

```json
{
  "routeDispatch": {
    "version": 1,
    "mode": "native-compiled",
    "artifact": {
      "kind": "slrt",
      "path": "routes.slrt",
      "hash": "sha256:..."
    },
    "routeCount": 2,
    "endpointCount": 2,
    "staticRoutes": 1,
    "parameterRoutes": 1,
    "catchAllRoutes": 0,
    "nativeNoJsEndpoints": 1,
    "urlGeneration": true,
    "urlWriters": 1,
    "dispatchStats": {
      "exactStaticPaths": 1,
      "parameterCandidateBuckets": 1,
      "segmentTrieNodes": 3,
      "staticEdgeStrategies": [
        "open-addressed-exact-hash",
        "segment-trie",
        "first-static-segment-bucket-fallback"
      ],
      "constraints": ["int"]
    },
    "json": {
      "request": {
        "native": 1,
        "generic": 0,
        "fallback": 0,
        "materialized": 1
      },
      "response": {
        "native": 1,
        "generic": 0,
        "fallback": 0
      }
    },
    "fallback": {
      "classicAvailable": true,
      "dynamicRoutes": 0,
      "partialRoutes": 0
    }
  }
}
```

`native-compiled` is a runtime optimization path, not a public route API. It
means Plan-backed native dispatch with `routes.slrt` integrity validation. The
native runtime validates `routes.slrt`, then builds an
exact-path hash index plus a method-specific segment trie from `app.plan.json`
route metadata. It preserves `HEAD` and `405` behavior, dispatches dynamic
handlers through the current V8 handler path, and executes provably static
literal `Results.text`, `Results.json`, and `Results.ok` handlers as native
responses. Candidate buckets remain as an internal fallback for partial or
manually constructed dispatch tables.

`routeDispatch.json` is aggregate visibility for JSON handling:

- `request.native` counts routes whose JSON body has schema metadata consumed
  by native validation before JavaScript.
- `request.generic` counts routes that accept JSON but lack a native schema
  plan and therefore use the generic body helper path.
- `request.fallback` counts routes whose request JSON plan is explicitly a
  fallback.
- `request.materialized` counts routes that still materialize a JavaScript body
  object after native validation.
- `response.native` counts routes with native static JSON response writer
  metadata. Schema-backed dynamic response writers are not emitted as native in
  this slice; imported `native-schema` response metadata is normalized to
  fallback with a reason.
- `response.generic` and `response.fallback` count routes that require the
  generic result serializer or an explicit fallback reason.

Named routes contribute native URL writers. The writer count is reported as
`urlWriters`; generated URLs percent-encode path parameter values and validate
the generated path through the native matcher.

## Route JSON Metadata

Compiler-emitted web Plan routes include JSON request and response plans:

```json
{
  "method": "POST",
  "pattern": "/users",
  "bindings": [
    { "kind": "body-json", "name": "body", "schema": "CreateUser" }
  ],
  "jsonRequest": {
    "mode": "native-schema",
    "schema": "CreateUser",
    "materialization": "materialize-once",
    "unknownFields": "ignore",
    "maxBodyBytes": 65536,
    "maxDepth": 50,
    "maxStringBytes": 4096,
    "maxArrayLength": 1024
  },
  "jsonResponse": {
    "mode": "native-static",
    "writer": "preencoded",
    "contentType": "application/json"
  }
}
```

`jsonRequest.mode` values:

- `none` means the route has no JSON request body.
- `generic` means the route accepts JSON but no compiler-visible body schema is
  available for native validation.
- `native-schema` means the native runtime validates the schema-backed JSON body
  before handler execution.
- `fallback` means the route cannot use native JSON request handling;
  `fallbackReason` explains why.

`jsonRequest.materialization` values:

- `none` for routes with no JSON body.
- `generic` for generic JSON helper materialization.
- `materialize-once` for native-validated bodies that still become one
  JavaScript body object through the existing body helper when the generated
  wrapper or handler needs it.
- `projected` is reserved for a future projected/lazy body representation and
  is not emitted by the current compiler.

`jsonRequest.unknownFields` is `ignore`, `reject`, or `passthrough`. The native
validator currently consumes `ignore` and `reject`; `passthrough` is preserved
as Plan metadata for future body projection work. Native request validation also
consumes schema string/number/integer/array bounds, literal/enum values,
optional/nullable object fields, and route-level JSON body/depth/string/array
limits. Validation failures produce safe problem details and do not echo raw
request values.

`jsonResponse.mode` values:

- `none` means the route has no JSON response metadata.
- `native-static` means a literal/static `Results.json` or `Results.ok` body is
  already represented as JSON bytes and can be written natively.
- `native-schema` is reserved for schema-backed native response writing. Current
  compiler output does not emit it for dynamic handler returns, and Plan loading
  normalizes it to `fallback` with `native-schema-response-writer-unsupported`
  when encountered.
- `generic` means the runtime must use the ordinary JSON result serializer.
- `fallback` means response metadata is known but unsupported for native
  writing; `fallbackReason` explains why.

`jsonResponse.writer` values are `none`, `preencoded`, `bounded`, and
`streamable`. Current native static responses use `preencoded`; dynamic handler
results with `.returns(...)` metadata are recorded as fallback instead of
pretending to have native response enforcement.

Typed handler metadata is compiler/Plan-first. For the current supported
typed-handler subset, the compiler emits a generated JavaScript wrapper that runs after
native Plan-backed validation and materializes typed route, query, header, body, and context
arguments from the request context. The wrapper creates one request service scope per
handler call. Literal `app.services.addSingleton/addScoped/addTransient("Token", factory)`
registrations in the source-input subset are emitted into the generated artifact, and
`Service<T>` parameters resolve through that scope with singleton, scoped, transient,
circular-dependency, and singleton-to-scoped diagnostics.

Provider injection opens provider handles through the existing stdlib/native bridge where
available: SQLite uses Plan-backed provider tokens, while PostgreSQL and SQL Server
materialize configured provider options from inferred database capability metadata and the
normal provider config key before opening the active bridge. Queue injection materializes an
inferred `queue.<name>` capability and, unless the source explicitly registers that token,
adds a default singleton service registration that is resolved through the request scope
and backed by `WorkQueue.create("<name>")`.
`Config<"KEY">` reads the matching environment value. If the compiler recorded a
literal config default for that key, generated JavaScript uses the default when
the environment value is absent. Custom validators, arbitrary TypeScript
lowering, controller constructor injection, dynamic middleware options, and
broader response writing remain future implementation work.

## Server Config Metadata

The runtime consumes Sloppy-owned server metadata emitted by the current compiler/config
pipeline for `sloppy run`: host, port, max connections, max request body bytes, request
timeout, keep-alive enablement, keep-alive idle timeout, max requests per connection, and
explicit inbound TLS listener settings.

TLS remains opt-in. Cleartext HTTP is the default. Enabling TLS requires
`Sloppy:Server:Tls:Enabled`, `Sloppy:Server:Tls:CertificatePath`, and
`Sloppy:Server:Tls:PrivateKeyPath`. Relative certificate and private-key paths in Plan metadata
are resolved under the loaded artifact directory; absolute paths are preserved when explicitly
emitted by config. Diagnostic output redacts TLS material path values, but these two server
path values remain Plan-visible today because the dev runtime consumes them directly.
Passphrase-protected private keys remain available only through the native
transport config until Sloppy has non-redacted runtime secret retrieval. Malformed, zero,
unsupported, range-overflowing, missing certificate/key, unsafe path, or embedded-NUL values fail
closed before serving work. Route-level limits and trusted proxy policy are not Plan metadata yet.

## Required Features

`requiredFeatures[]` records runtime features that must be active before execution. The
runtime feature registry rejects missing features before engine initialization. Current
feature families include the bootstrap stdlib, filesystem, time, crypto, codec, network,
OS/process, workers, HTTP transport, provider-related features where implemented, and
`node.compat.<builtin>` entries for Node compatibility shims used by the graph.

Feature descriptors are validation boundaries. A Plan feature entry declares
the feature required by the compiled artifact and validated by the runtime.

## Program Plans

Program Plans set top-level `"kind": "program"`, emit empty `handlers` and
`routes` arrays, and include metadata like:

```json
{
  "metadata": {
    "completeness": {
      "status": "opaque",
      "reasons": [
        {
          "code": "program-mode",
          "message": "program mode does not require static web route metadata"
        }
      ]
    },
    "program": {
      "entry": "src/main.ts"
    }
  }
}
```

The runtime skips web route-table startup validation for Program Plans. CLI
inspection commands report the absence of routes as intentional. OpenAPI is
web-only and fails clearly for Program Plans.

Program runtime execution calls a named `main` export first, then a default
function export, then relies on top-level module execution. Function entrypoints
receive `(args, ctx)`, where `args` comes from `sloppy run ... -- <args>` and
`ctx` records `kind`, `args`, `cwd`, `environment`, and
`plan.metadataCompleteness`. The Program console writes stdout/stderr through
the CLI, and numeric returns in the range `0..255` set the process exit code.

## Providers And Capabilities

Plan provider and capability metadata supports policy checks and doctor/audit visibility.
SQLite, PostgreSQL, and SQL Server provider/capability metadata is consumed by scoped V8
provider bridges when their runtime features are active. Filesystem and network capability
metadata currently represents Sloppy policy/metadata. OS sandbox enforcement is
future scoped work.

Program Mode emits simple stdlib capability entries from runtime stdlib imports
and may also preserve simple `sloppy.json` stdlib capability declarations as
Plan capability entries. The current simple declarations map to
`filesystem/readwrite`, `network/connect-listen`, `os/info`, and `use` access
for `time`, `crypto`, `codec`, `workers`, and `ffi`.

## Native FFI Metadata

`native.ffi[]` records compiler-extracted native library declarations from
`sloppy/ffi`. Each function entry carries its Plan-visible ID, JavaScript name,
native symbol, calling convention, return type, parameter types, and source
location.

```json
{
  "requiredFeatures": ["stdlib.ffi"],
  "capabilities": [
    { "token": "ffi", "kind": "ffi", "access": "use" }
  ],
  "native": {
    "ffi": [
      {
        "name": "sloppy_ffi_test",
        "convention": "system",
        "functions": [
          {
            "id": "ffi:sloppy_ffi_test:addI32",
            "name": "addI32",
            "symbol": "sloppy_ffi_add_i32",
            "convention": "system",
            "return": "i32",
            "parameters": ["i32", "i32"]
          }
        ]
      }
    ],
    "ffiStructs": [
      {
        "name": "Point",
        "layout": "sequential",
        "pack": 4,
        "fields": [
          { "name": "x", "type": "i32" },
          { "name": "y", "type": "i32" }
        ]
      }
    ]
  }
}
```

The native Plan parser validates known FFI type names, non-empty library and
symbol names, supported calling conventions, unsupported return buffer types,
and sequential struct layout metadata. `stdcall` is accepted as metadata but is
runtime-supported only on Windows builds with libffi stdcall support.

Plan FFI metadata is not a memory-safety proof. It is the typed contract the
runtime uses to resolve libraries, cache symbols, prepare libffi call
interfaces, and expose audit/doctor/capabilities rows.

Typed provider parameters such as `Postgres<"main">`, `Sqlite<"main">`, and
`SqlServer<"main">` are represented as route injections and inferred Plan
provider/capability requirements. The normal case does not require
`builder.capabilities.addDatabase(...)` or `app.use(sqlite(...))` boilerplate:
`Postgres<"main">` implies `postgres/main` plus `data.main`,
`Sqlite<"main">` implies `sqlite/main` plus `data.main`, and
`SqlServer<"main">` implies `sqlserver/main` plus `data.main`. `WorkQueue<"emails">`
implies a `queue.emails` capability. Runtime injection depends on normal provider config
and the active provider bridge for the current runtime path. PostgreSQL and SQL Server typed
injection use the inferred config keys
`Sloppy:Providers:postgres:<name>:connectionString` and
`Sloppy:Providers:sqlserver:<name>:connectionString`; SQLite uses
`Sloppy:Providers:sqlite:<name>:database` unless inline/manual provider metadata is used
for an explicit policy override.

Credential-bearing fields must not be persisted in Plan metadata. Use redacted values,
config references, or secret-source references. Generated Framework provider wrappers carry
the logical config key and the runtime environment variable name needed to resolve PostgreSQL
or SQL Server connection strings; they must not embed the connection string value in generated
JavaScript or Plan/package metadata.

## Dependency Graph

Plans can include `dependencyGraph` and `dependencyGraphArtifact` when the
compiler bundles installed packages, included modules/assets, or Node
compatibility shims:

```json
{
  "dependencyGraph": {
    "schemaVersion": 1,
    "packages": [],
    "modules": [],
    "assets": [],
    "nodeBuiltins": [],
    "compatibilityFindings": []
  },
  "dependencyGraphArtifact": {
    "path": "deps.graph.json",
    "hash": "sha256:..."
  }
}
```

The embedded graph is the authoritative Plan-visible summary. The companion
artifact is useful for package manifests and external tooling. Metadata and
source-map entries must use normalized repo-relative IDs, not absolute local
paths.

Dependency graph findings are surfaced by `sloppy audit`, `sloppy doctor`, and
`sloppy deps`. See [Dependency graph](dependency-graph.md) for the field-level
reference.

## Source Input

`sloppy run <source.js>` invokes `sloppyc build`, writes generated artifacts to
`.sloppy` unless `sloppy.json` or `--out` selects another output directory,
validates them through the same Plan/artifact path, and then runs artifacts. It
is a development shortcut over the artifact path, not a watch mode, cache
policy, package manager, or full TypeScript build system.

## Current Limits

- Package-manager install, registry, lockfile, and semver-solving behavior.
- Full Node runtime/package identity.
- Unrestricted import graph resolution outside the sealed artifact graph.
- Public plugin extension points.
- Public release hardening.
- Benchmark-backed performance comparisons.

## Source Docs

- `docs/internals/compiler.md`
- `docs/reference/supported-syntax.md`
- `docs/internals/runtime.md`
- `docs/internals/security-model.md`
- `docs/internals/platform-boundaries.md`
