# Security And Permissions

## Purpose

Sloppy should make authority explicit. Application code should receive capabilities through
configuration, modules, and services rather than ambient global power.

This model improves auditability and diagnostics. It is not a claim of complete OS-level
sandboxing.

## Scope

This document covers:

- capability model;
- permission grants;
- filesystem capabilities;
- data provider capabilities;
- environment/config secrets;
- no JS raw pointers;
- resource ID validation;
- generation counters;
- honest sandbox wording;
- future OS sandboxing;
- audit tooling;
- diagnostics;
- testing and acceptance criteria.

## Non-Goals

The foundation phase does not implement:

- OS sandboxing;
- full security audit tooling. The current `sloppy audit` command is metadata-only and
  does not enforce permissions or execute user code.

## Current Phase

EPIC-15 implements a bootstrap JavaScript capability metadata skeleton for database
capabilities only, and EPIC-19 adds metadata-only `sloppy audit` fixture output.
MAIN1-02 adds native Plan v1 alpha parsing and validation for `dataProviders` and
`capabilities` sections. MAIN1-10 turns that metadata into an immutable runtime capability
registry and explicit provider-bridge check hooks. Those checks are real where callers pass
the registry before provider work begins. ENGINE-05 wires the V8 SQLite bridge to the
existing database hook; it does not add a new policy engine. CORE-FS-01.C/D/H makes
`stdlib.fs` Plan-visible and adds the first native filesystem operations plus the
feature-gated JavaScript bridge. CORE-FS-01.E/F extends that bridge to Directory,
FileHandle, temp, atomic, symlink, and native lock primitives. CORE-FS-01.G adds
resource-backed watch handles under the existing `fs.watch` capability. CORE-FS-01.I/J
adds deterministic filesystem doctor/audit goldens for Plan-visible `readwrite`,
`watch`, and `delete` metadata. The runtime enforces the current development/strict path
policy for implemented filesystem operations. Network capabilities remain
Plan-visible policy metadata:
they can be stored and checked by token/kind/access, and CORE-NET-01.I adds TCP
doctor/audit evidence, but no permission prompt or OS sandbox exists.

## Future Phase

The first implementation should model capabilities and permission declarations before
exposing filesystem or database APIs to user handlers.

## Capability Model

Capability metadata is a runtime contract, not intended ceremony for ordinary app authors.
The engine must fail closed when capability metadata is missing or invalid, but future
framework/compiler work must generate common provider capability entries from
provider/module declarations. Handwritten capability blocks should be necessary only for
advanced policy shaping, plugin/provider hardening, split grants, or explicit production
least-privilege review.

Post-Core framework capability layers are:

1. inferred capabilities from recognized provider calls;
2. provider config/access policy, for example `app.use(sqlite("main", { access:
   "readwrite" }))`;
3. explicit advanced policy for unusual or locked-down cases.

Every inferred capability must be Plan-visible and later inspectable through doctor,
routes, audit, or capabilities tooling. If inference is not safe, compilation must fail or
require explicit metadata; no silent unsound inference.

FRAMEWORK-01.B keeps normal provider configuration on the inferred path:
`app.use(sqlite("main"))` binds `Sloppy:Providers:sqlite:main`, the compiler emits the
matching provider/capability metadata, and missing required provider config fails before
provider work. Application config may contain secrets later, but this slice does not add a
secrets manager, user secrets, remote providers, or raw environment access from JS.
Secret-looking config values are redacted in diagnostics and Plan metadata.

CORE-CRYPTO-01.I completes the scoped implemented crypto API without adding a new
permission grant type. `sloppy/crypto` is Plan-visible as `stdlib.crypto` and registers
`__sloppy.crypto` only for active V8 plans. Crypto backends use OS random/crypto
facilities or vetted dependencies behind Sloppy-owned platform/backend boundaries.
Password hashing uses `libsodium` Argon2id PHC strings, but JavaScript never receives raw
native pointers or backend handles. Secure `Hash`/`Hmac` APIs and `NonCryptoHash` are
separate namespaces so non-security hashes are not confused with signatures, tokens,
password hashes, or integrity checks. `NonCryptoHash.xxHash64` uses the vetted `xxhash`
dependency and emits a Plan-visible doctor warning when statically visible source uses it
in security-looking contexts. That warning is a best-effort static cue, not comprehensive
security enforcement.

Secret-bearing crypto inputs must not appear in Plan metadata, diagnostics, logs, examples,
or goldens. `Secret` disposal is best-effort cleanup of Sloppy-owned native buffers only:
it cannot erase prior JS string copies, engine internals, caller-owned buffers, operating
system paging, or crash dumps. Password hashing runs through an async/offload path and does
not block the V8 owner thread in the V8 stdlib surface.

Current random evidence is limited to OS-source use and API shape: UUID version/variant,
token/hex/numeric alphabets, and rejection-sampling implementation. Deterministic tests do
not claim random quality.
CORE-NET-01.A/B adds the TCP networking API contract and policy model. `sloppy/net` is
Plan-visible as `stdlib.net` and unavailable by default until native TCP backends and V8
intrinsics land. Development mode allows ordinary loopback workflows without handwritten
target declarations. Strict mode can require explicit allow rules for external connects
and listens, and denied operations must fail before native socket admission. Statically
visible literal host/port targets are future Plan-visible network capability metadata;
dynamic host/port values must be represented honestly as partial/dynamic metadata rather
than guessed.

CORE-NET-01.G adds native local DNS/address handling and JS `NetworkAddress` parsing.
CORE-NET-01.I adds network doctor/audit goldens for explicit `connect`, `listen`, and
`connect-listen` Plan capabilities plus source examples for strict policy shape, but does
not broaden the permission claim: default tests remain loopback/local, and external
network access still needs strict-mode policy/admission work before it can be treated as a
completed security boundary.

CORE-HTTPCLIENT-01.A/B/C adds an outbound HTTP client contract on top of the network
policy vocabulary. `HttpClient` imports from `sloppy/net` are Plan-visible as
`stdlib.httpclient`, separate from raw TCP `stdlib.net`, because named clients,
pipeline/transport policy, TLS validation, redirects, pooling, and redaction have a
different security surface. CORE-HTTPCLIENT-01.D adds the first cleartext `http://`
HTTP/1.1 request/response lane over the CORE-NET TCP bridge. Later slices add bounded
helper APIs, buffered request/response stream shapes, operation-wide timeout/deadline/
cancellation, per-origin HTTP/1.1 pooling, bounded redirects, DNS failure mapping,
strict-network preconnect denial, cross-origin sensitive-header strip/deny defaults, and
HTTP client doctor/audit metadata goldens. HTTPS/TLS, proxy policy, true socket-level
streaming, automatic compiler inference of static target literals, external-network
security evidence, and a separate HTTP-native V8 bridge remain deferred. The current V8
surface activates `stdlib.httpclient` through the existing private `__sloppy.net` bridge.
Doctor/audit
output represents static targets and dynamic/partial targets honestly and must redact or
omit authorization headers, cookies, API keys, bearer tokens, config-backed secret values,
secret query parameters, and TLS-sensitive material.

Network policy is not an OS sandbox claim. JavaScript never receives raw sockets, libuv
handles, OS handles, raw native pointers, or backend resource internals. DNS/connect work
must not block the V8 owner thread, and doctor/audit output must redact sensitive endpoint
details where policy requires it. Current network doctor/audit goldens prove metadata
visibility, not OS-level containment.

CORE-NET-02 extends the same network permission model to local IPC policy. Local endpoints
use named-root paths such as `runtime:/my-app.sock`; strict policy must deny unallowed
local endpoint connect/listen before native backend admission, and dynamic paths must be
marked partial/dynamic rather than guessed. Unix domain socket and Windows named pipe
backends are platform-gated execution evidence, not OS sandbox evidence.

CORE-CODEC-01.A/B adds the codec API contract without adding a new permission grant type.
`sloppy/codec` is Plan-visible as `stdlib.codec`; encoding, text, binary, compression, and
checksum helpers are data transforms, not permission grants. CORE-CODEC-01.H/J implements
CRC32 as a non-security utility and emits a static doctor warning when `Checksums.crc32`
is visible in security-looking contexts. Checksums must not be used or documented as
authentication, HMAC, signatures, password hashing, tokens, cryptographic hashes, or
attacker-resistant integrity. Security APIs remain under `sloppy/crypto`.

CORE-OS-01.A/B defines the OS API policy model. CORE-OS-01.C/H makes `sloppy/os`
Plan-visible as `stdlib.os` for System, Environment, and V8 process runtime use.
CORE-OS-01.D adds explicit-argv `Process.run` admission under `process.run`; strict policy
denies it unless that authority is explicitly enabled. OS authority categories are
`os.info`, `env.read`, `env.list`, `process.run`, `process.shell`, `process.signal`,
`process.kill`, and `signals.shutdown`. Raw environment access is lower-level than app
configuration, process execution requires explicit argv, shell execution is absent or
separately gated, and diagnostics must never print environment values, secret args,
sensitive captured output, native process handles, pipe handles, raw PIDs-as-authority,
libuv handles, OS handles, or native pointers. The V8 bridge exposes only slot/generation
resource IDs for process handles. These are Sloppy admission checks and audit facts, not
OS sandboxing.

COMPILER-30.E keeps this metadata-only: supported config reads, schema declarations,
request bindings, and `Results.*` response facts are Plan-visible for later audit and
completeness work, but this slice does not enforce provider/capability effects or runtime
request validation.

COMPILER-30.F/G starts the inferred capability path for provider calls in the compiler.
Static database provider handles and same-file helpers can now produce route `effects[]`
metadata with capability kind, provider kind, token, operation, and access. SQLite,
PostgreSQL, and SQL Server metadata are representable; only SQLite has an executable
generated runtime opener today. Generated PostgreSQL and SQL Server provider-backed route
wrappers are rejected until those JS bridges exist. This remains Plan-visible inference and
request-scoped provider handle generation where supported; it does not add OS sandboxing,
new provider bridges, non-database provider adapters, or a broad manual policy system.

COMPILER-30.H/I validates that inferred provider effects have matching Plan-visible
provider/capability registrations before artifacts are emitted. Missing provider truth is
invalid, while optional gaps such as unknown response shape or body schema are represented
as partial completeness. Capability facts are emitted by kind rather than by SQLite-specific
assumption, so future provider families can plug into the same Plan shape once their
compiler and runtime adapters exist.

A capability is a named authority token. Code receives a capability through services or
explicit context, not through global APIs.

Example:

```ts
export const FilesModule = Sloppy.module("files")
  .capabilities(caps => {
    caps.addDir("files.storage", "./uploads", {
      read: true,
      write: true,
    });
  })
  .services(services => {
    services.addScoped("files.storage", scope => {
      return scope.capabilities.dir("files.storage");
    });
  });
```

The token `files.storage` should appear in the Sloppy Plan with source location and access
modes.

Implemented bootstrap database capability shape:

```ts
const DataModule = Sloppy.module("data")
  .capabilities(caps => {
    caps.addDatabase("data.main", {
      provider: "sqlite",
      database: ":memory:",
      access: "readwrite",
    });
  });
```

Capability tokens must be non-empty strings, duplicates fail, and
`app.capabilities.has/get/list` exposes frozen debug metadata with the declaring module when
applicable. SQLite JavaScript execution now checks the database hook before native
open/read/write and transaction work when the engine has Plan/capability metadata. If the
hook inputs are absent, SQLite bridge calls fail closed. Filesystem policy is owned by
CORE-FS-01: the current core file operations apply the development/strict path model in
`docs/project/filesystem-api-architecture.md`. The V8 bridge accepts an optional borrowed
`SlEngineOptions.filesystem_policy`; omitted policy uses the documented development
fallback roots for low-level bridge tests only, while richer app-host config plumbing and
doctor/audit reporting remain future work.

CORE-FS-02 clarifies that trusted runtime/bootstrap file access is outside the app
filesystem grant model. Plan loading, bundle and source-map loading, bootstrap stdlib
asset loading, source-input `sloppy.json`, compiler handoff artifact inspection, and
diagnostic source-map bytes must not require app `stdlib.fs` activation or app filesystem
capabilities. Strict filesystem policy applies to app-owned `sloppy/fs` operations, not to
runtime-owned artifacts required before the Plan and feature set are known. Runtime reads
may reuse low-level native filesystem helpers for path conversion and stable errors, but
they must not enter the public V8 `__sloppy.fs` bridge.

## Permission Grants

Permission grants are the runtime/configured allowance for a capability to exist.

Future grant sources may include:

- CLI flags;
- config files;
- environment-selected profiles;
- deployment manifest;
- module defaults for safe built-ins.

Grants must be visible to `sloppy audit` and diagnostics.

## Filesystem Capabilities

Filesystem capabilities specify or infer:

- token;
- root path;
- access category: read, write, append, delete, list, metadata, watch, or lock;
- path normalization policy;
- symlink policy;
- source module.

Core runtime code must use Sloppy platform file/path abstractions. No direct OS file APIs in
core.

## Data Provider Capabilities

Database providers contribute capabilities:

- provider name;
- token, such as `data.main`;
- config key references;
- lifetime;
- permissions required by routes/modules.

Plan entries must reference config keys, provider tokens, or already-redacted placeholders,
not connection string values. Raw secrets do not belong in `app.plan.json`.

Bootstrap metadata does not validate live config keys. Runtime capability checks can deny
database access by token, provider, and read/write mode before a caller invokes provider
work. The V8 SQLite bridge now calls those hooks for open, exec, query, queryOne, and
transaction operations when the app host passes the parsed Plan and capability registry
into the engine.

Async/offloaded provider work performs a capability check before executor admission.
Provider executors require a capability-check hook and a configured provider token. The
executor is generic native-provider/offload infrastructure: it enforces that policy runs
before admission, but database/filesystem/network/native-addon policy stays outside the
executor instead of being hardcoded into `provider_executor`. Database providers pass the
immutable Plan-backed capability registry to their hook. No provider may bypass capability
checks, queue capacity, cancellation/deadline state, or provider-executor shutdown policy.
A denied capability fails before queue slot reservation, ownership transfer, or provider
worker execution; overflow and shutdown remain separate runtime states, not permission
failures.

Database access policy is deliberately small:

- read operations require `read` or `readwrite`;
- write operations require `write` or `readwrite`;
- readwrite operations require `readwrite`;
- SQLite stores the requested resource access mode after open; `read` resources deny write
  operations and `write` resources deny read operations before provider work even when the
  underlying SQLite native handle has broader technical capability;
- provider-token mismatch rejects even when the access mode matches;
- provider-kind mismatch rejects before execution;
- wrong capability kind and missing capability reject before execution.

Provider executor denial diagnostics may include capability token, operation name,
provider instance id/name, provider kind, required access, actual safe metadata, and denial
reason. They must not include SQL parameter values, connection strings, passwords, access
tokens, raw provider handles, or native pointers.

The SQLite hook is intentionally narrow: provider-specific V8 bridge code lives in
`src/engine/v8/intrinsics_<provider>.cc`, while `engine_v8.cc` stays provider-neutral.
Future PostgreSQL and SQL Server bridge checks should follow the same pattern instead of
embedding provider policy in the engine core.

## Environment And Config Secrets

Secrets must be handled as secret values:

- connection strings are redacted;
- env var values are not printed;
- diagnostics may show key names like `DATABASE_URL`;
- plan artifacts must not contain raw secret values;
- logs default to redaction for known secret fields.

Diagnostic redaction currently covers representative password, `PWD`, token, `SECRET`,
`API_KEY`, generic `key`, `connectionString`/`connection_string`, and URI userinfo
password forms. This is a deterministic audit helper for known diagnostic paths, not a full
process-wide secret scanner. New provider/runtime diagnostics that include user
configuration must either pass through provider-specific redaction or the shared diagnostic
redaction helper before rendering text or JSON.

## Native Handles And Resource IDs

JavaScript must never receive raw native pointers.

JS-visible native resources use resource IDs validated by the resource table. Resource IDs
must include generation counters eventually so stale handles can be detected after close and
reuse.

Invalid, stale, closed, or wrong-kind resource IDs must fail with diagnostics.

V8 provider bridges consume the engine-owned resource table from inside `src/engine/v8/`.
They may expose only opaque resource ID handles to JavaScript, and they must not create
provider-specific pointer maps or embed provider access policy directly in `engine_v8.cc`.

## Runtime Permissions Are Not A Full Sandbox

Sloppy permission checks reduce accidental and application-level authority. They do not
automatically confine the entire process from every operating system side effect.

Documentation and diagnostics must be honest:

- "permission denied by Sloppy capability policy" is acceptable;
- "this process is sandboxed" is not acceptable unless OS sandboxing exists.

## Future OS Sandboxing

Future options may include:

- Windows job objects/AppContainer-style exploration;
- Linux seccomp/namespaces exploration;
- macOS sandbox profiles exploration.

These are deferred until the runtime and capability model are stable.

## Audit Tooling

Current metadata-only command:

```powershell
sloppy audit --plan app.plan.json
```

Current fixture-driven output can list static metadata from an app plan. It does not
compile source files, execute user handlers, or enforce permissions.

MAIN1-11 hardens the alpha audit scope without turning it into a full policy engine.
ENGINE-20.C consumes COMPILER-30 effects and completeness metadata: `sloppy capabilities`
shows generated provider effects as inferred route capabilities, and `sloppy audit` may
report missing route handlers, duplicate route metadata, missing provider capability
references, capability/provider mismatches, insufficient provider access metadata,
partial/invalid Plan completeness, body bindings without schema metadata, unknown response
metadata, and filesystem/network skeleton notes from plan metadata. ERROR findings return
nonzero for CI/static review. It still does not claim live provider reachability,
auth/RBAC/security-scheme generation, OS sandboxing, or runtime permission success.

Planned hardened output:

- module list;
- declared capabilities;
- routes reachable from each capability;
- provider tokens and driver requirements;
- missing grants;
- dynamic mode warnings.

`sloppy audit` should use compiler/app-host emitted Sloppy Plan metadata and must not
execute user handlers.

## Error And Diagnostic Behavior

Diagnostics should cover:

- missing capability grant;
- denied filesystem path;
- denied database token;
- stale resource ID;
- wrong resource kind;
- secret redaction;
- dynamic mode authority warning.

Example:

```text
error[SLP_PERMISSION_DATABASE_DENIED]: database permission was not granted

  Token:
    data.main

  Route:
    GET /users/{id:int}

  Handler:
    Users.Get

help: add a database capability grant for data.main or remove the route dependency
```

## Testing Requirements

Security/permission tests must include:

- allowed capability use;
- denied capability use;
- path normalization edge cases;
- stale resource ID;
- wrong resource kind;
- redacted connection string;
- audit fixture output;
- dynamic mode warning.

Current ENGINE-19.D executable coverage is visible through
`conformance.capability.native_registry`, `conformance.capability.provider_executor`,
V8-gated `conformance.sqlite.denied_capability`, and V8-gated localhost
`conformance.users_api_sqlite.localhost_transport`. These names prove the current Sloppy
capability checks before provider work. CORE-FS-01.E/F adds native `core.filesystem`
coverage and V8-gated filesystem smoke coverage for core, directory, and FileHandle API
paths. CORE-FS-01.G adds resource-backed watch handle coverage in native and V8-gated
filesystem tests. CORE-FS-01.I/J adds filesystem-specific doctor/audit goldens and
source examples. These gates do not prove OS sandboxing, PostgreSQL/SQL Server JavaScript
bridges, live providers, package readiness, or runtime behavior for source examples that
remain outside the supported compiler subset.
CORE-FS-02 adds a runtime-artifact boundary check showing the artifact loader reaches V8
feature failure without an active app filesystem feature; that evidence is separate from
app strict-policy denial tests.

## Quality Gates

- no JS raw native pointers;
- resource IDs validated at every boundary;
- capability diagnostics have stable codes;
- plan fixtures never include secrets;
- platform-specific path behavior is tested behind platform abstraction.

## Implementation Tasks

1. Define capability and permission plan schema.
2. Define resource ID model and generation counters.
3. Add filesystem capability structs after path abstraction exists.
4. Add database capability entries during provider phases.
5. Add diagnostics and snapshot tests.
6. Harden `sloppy audit` after compiler/app-host capability metadata exists.

## Acceptance Criteria

Permissions foundation is accepted when:

- plan schema can describe filesystem and database capabilities;
- runtime can reject missing permissions before use;
- resource IDs detect stale/closed handles;
- diagnostics are actionable and redacted;
- docs clearly state that Sloppy permissions are not OS sandboxing.

## Open Questions

- Exact CLI grant syntax.
- Symlink policy.
- Whether default dev mode grants are interactive prompts or explicit config.
- How OS sandboxing, if any, composes with Sloppy capabilities.
