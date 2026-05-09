# Application Plan

The Sloppy Plan is the compiler/runtime contract. It is deterministic metadata consumed by
native validation, CLI inspection, runtime feature activation, and V8 artifact execution.

## Purpose

The Plan lets Sloppy validate application shape before runtime execution:

- route metadata and handler IDs;
- generated artifact names and hashes;
- required runtime features;
- provider and capability metadata;
- compiler/source metadata for diagnostics;
- configuration metadata where the compiler emits it.

The Plan is not a package manifest, npm metadata format, Node runtime layer, or public
extension system.

## Current Runtime Contract

Plan v1 alpha remains the current runtime contract. Supported artifacts contain
`app.plan.json`, generated `app.js`, and source-map metadata where available. The runtime
validates schema version, route/handler/provider/capability structure, artifact hashes,
runtime target support, and required features before entering V8.

Handler IDs are compiler-owned numeric IDs. Current executable V8 dispatch uses generated
artifact registration and registered handler dispatch; direct numeric handler-call ABI
entry remains unsupported for the noop engine.

## Routes And Handlers

The compiler can emit route metadata for the supported source subset:

- literal route patterns;
- supported HTTP methods;
- optional route names;
- source metadata;
- direct handler IDs;
- simple request context usage in supported handlers.
- Framework v2 compiler-inferred metadata for typed handlers, including route/body/query/header
  bindings, context bindings, provider/queue injection requirements, schema definitions,
  semantic validation/redaction metadata, and visible `Results.*` response metadata.

The native runtime validates route metadata, builds a deterministic route table, and
dispatches supported requests through V8 in the V8 lane. For Framework v2 metadata, the
native dispatcher also consumes Plan-backed route/query/header scalar bindings and
schema-backed JSON body metadata to fail invalid requests with a safe `400`
`application/problem+json` response before calling the handler. Dynamic route registration,
middleware, modules, arbitrary imports, custom validators, and full TypeScript semantics
remain outside the current source subset.

Typed Framework v2 handler metadata is compiler/Plan-first. For the current supported
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
`Config<"KEY">` reads the matching environment value. Custom validators, arbitrary
TypeScript lowering, controller
constructor injection, and broader response writing remain separate implementation lanes.

## Server Config Metadata

The runtime consumes Sloppy-owned server metadata emitted by the current compiler/config
pipeline for `sloppy run`: host, port, max connections, max request body bytes, request
timeout, keep-alive enablement, keep-alive idle timeout, max requests per connection, and
explicit inbound TLS listener settings.

TLS remains opt-in. Cleartext HTTP is the default. Enabling TLS requires
`Sloppy:Server:Tls:Enabled`, `Sloppy:Server:Tls:CertificatePath`, and
`Sloppy:Server:Tls:PrivateKeyPath`. Relative certificate and private-key paths in Plan metadata
are resolved under the loaded artifact directory; absolute paths are preserved when explicitly
emitted by config. Passphrase-protected private keys remain available only through the native
transport config until Sloppy has a non-redacted runtime secret retrieval lane. Malformed, zero,
unsupported, range-overflowing, missing certificate/key, unsafe path, or embedded-NUL values fail
closed before serving work. Route-level limits and trusted proxy policy are not Plan metadata yet.

## Required Features

`requiredFeatures[]` records runtime features that must be active before execution. The
runtime feature registry rejects missing features before engine initialization. Current
feature families include the bootstrap stdlib, filesystem, time, crypto, codec, network,
OS/process, workers, HTTP transport, and provider-related features where implemented.

Feature descriptors are evidence boundaries. A Plan feature entry does not prove that every
possible API surface is implemented; it only declares the feature required by the compiled
artifact and validated by the runtime.

## Providers And Capabilities

Plan provider and capability metadata supports policy checks and doctor/audit visibility.
SQLite, PostgreSQL, and SQL Server provider/capability metadata is consumed by scoped V8
provider bridges when their runtime features are active. Filesystem and network capability
metadata must not be presented as complete OS-sandbox evidence unless a scoped
implementation and evidence lane prove it.

Framework v2 typed provider parameters such as `Postgres<"main">`, `Sqlite<"main">`, and
`SqlServer<"main">` are represented as route injections and inferred Plan
provider/capability requirements. The normal case does not require
`builder.capabilities.addDatabase(...)` or `app.use(sqlite(...))` boilerplate:
`Postgres<"main">` implies `postgres/main` plus `data.main`,
`Sqlite<"main">` implies `sqlite/main` plus `data.main`, and
`SqlServer<"main">` implies `sqlserver/main` plus `data.main`. `WorkQueue<"emails">`
implies a `queue.emails` capability. Runtime injection depends on normal provider config
and the active provider bridge for the current lane. PostgreSQL and SQL Server typed
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

## Source Input

`sloppy run <source.js>` invokes `sloppyc build`, writes generated artifacts, validates
them through the same Plan/artifact path, and then runs artifacts. It is a development
shortcut over the artifact path, not a watch mode, cache policy, package manager, or full
TypeScript build system.

## Non-Goals

- Node/Bun/Deno/npm behavior.
- Package-manager metadata.
- Arbitrary import graph resolution.
- Public plugin extension points.
- public release or production-readiness claims.
- Performance claims from Plan validation or artifact smoke tests.

## Source Docs

- `docs/compiler.md`
- `docs/compiler-supported-syntax.md`
- `docs/execution-model.md`
- `docs/security-permissions.md`
- `docs/internals/platform-boundaries.md`
