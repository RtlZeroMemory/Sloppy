# Sloppy Plan

## Purpose

`app.plan.json` is the compiler/runtime contract. It describes the native host graph that
the runtime validates and builds before user code handles work.

The plan exists so the runtime can dispatch routes natively, audit permissions, validate
configuration, check runtime compatibility, and call JavaScript handlers by numeric ID.

## Scope

This document defines the intended plan v1 shape, validation rules, compatibility rules,
compiler emission rules, implementation tasks, and acceptance criteria for the first plan
loader.

## Non-Goals

The foundation phase does not implement:

- file-based plan loading;
- route matching;
- service graph execution;
- module extraction;
- database providers.

## Current Phase

TASK 06.B implements JSON parsing and minimal shape validation for handwritten Plan v1
bytes. The parser exposes an arena-owned `SlPlan` and handler table to later runtime work.
TASK 08.A adds a V8-gated handwritten smoke path that parses an `app.plan.json` fixture,
evaluates handwritten `app.js`, maps handler ID `1` to the plan `exportName`, and invokes
that global JavaScript function through the engine boundary.
EPIC-21 adds the first `sloppyc build` compiler output. The emitted `app.plan.json`
matches the required minimal handler/bundle/source-map fields and includes route metadata
for EPIC-22 handoff.
MAIN-01 locks `examples/compiler-hello/app.js` as the canonical verification fixture for
that contract: the emitted plan references `app.js`, `app.js.map`, handler ID `1`, and a
single `GET /` route bound to that handler. The plan remains deterministic and does not
carry absolute source paths or timestamps.
EPIC-24 keeps Plan v1's handler schema stable but changes V8 execution from named global
lookup to runtime-owned registration. Generated app artifacts call
`__sloppy_register_handler(id, handler)`, and `sloppy run` validates that each plan handler
ID has a registered callable before accepting requests or running `--once`. Generated apps
also keep the legacy `globalThis.__sloppy_handler_<id>` export so no-context
runtime-contract callers continue to use the plan's `exportName` contract explicitly.
MAIN1-02 hardens that contract for alpha startup validation: the native parser now owns the
Plan v1 alpha `routes`, `dataProviders`, and `capabilities` sections when present, and the
supported `sloppy run --artifacts` path verifies `sha256:` hashes for `app.js` and
`app.js.map` before creating the V8 engine. MAIN1-03 adds a native app-host startup
validation pass over the parsed Plan before route materialization, V8/user execution, or
request serving. Minimal handler-only Plan v1 fixtures still parse for backward
compatibility, but runnable artifact apps must provide route metadata.
ENGINE-02.E makes `sloppy run <source>` and `sloppy run` with `sloppy.json` compile first
and then reuse this exact artifact validation path. Source-input run still treats
`app.plan.json` as the runtime source of truth; it does not introduce runtime source-file
discovery.
ENGINE-02 expands compiler-emitted Plan metadata without widening the runtime execution
claim: supported apps can now emit GET/POST/PUT/PATCH/DELETE route metadata, async handler
flags, declaration source locations, feature flags, and minimal SQLite
data-provider/capability metadata. The current dev host still serves GET route metadata
only. ENGINE-06 uses provider/capability entries for V8 SQLite bridge enforcement; other
providers remain metadata until their JavaScript bridges exist.
ENGINE-22.C keeps the public Plan struct shape as borrowed `SlStr` views, but the native
parser now uses the shared arena string-copy primitive for JSON string ownership. The
`sloppy run --artifacts` loader interns stable metadata after parsing and validation
succeed: version/target strings, artifact identifiers, handler names, route
methods/patterns/names, provider tokens/names, service/capability metadata, and capability
token/kind/access/provider metadata. It does not intern artifact paths, hashes, source-map
paths, provider database names, secrets, request data, connection strings, or transient
diagnostics.
FRAMEWORK-01.B adds compiler-emitted configuration metadata. `sloppyc` records the
selected environment, declared/effective config keys, each key's winning source layer, and
provider bindings such as `Sloppy:Providers:sqlite:main`. Sensitive-looking values are
redacted. The native Plan v1 parser intentionally ignores this metadata for now; broader
typed Plan graph expansion remains a Strong Plan follow-up under #355-#359.

COMPILER-30 (#460) defines the compiler-owned inference path that should feed the next
Plan metadata expansion. The compiler, not the runtime, owns inference for routes, route
groups, function modules, providers, config keys, request binding, schemas, results,
function effects, capabilities, source locations, diagnostics, and completeness. Strong
Plan issues #355-#359 consume that output for typed graph representation, validation,
doctor/audit, OpenAPI/optimization hooks, and versioning.
COMPILER-30.H/I adds the first compiler-emitted strong metadata layer: `modules`,
`sourceFiles`, route and plan `completeness`, provider-kind-aware `effects`, and
`strongPlan` evidence/compatibility metadata. ENGINE-27.A/B adds native
`requiredFeatures[]` storage and a runtime feature registry. The parser accepts non-empty
required-feature arrays, while app-host/runtime startup fails closed when a required feature
is unknown, unavailable, or missing an unavailable dependency.
ENGINE-27.C/D extends feature descriptors with current stdlib import and V8-intrinsic
metadata. Plan activation is still derived from supported Plan target/route/provider
metadata plus explicit `requiredFeatures[]`; the runtime does not add package-manager
semantics or arbitrary import resolution. CORE-FS-01.A/B adds the `stdlib.fs` descriptor
and lets the compiler emit that required feature when source imports `sloppy/fs`.
CORE-TIME-01.A/B adds the `stdlib.time` descriptor and lets the compiler emit that
required feature when source imports `sloppy/time`. CORE-TIME-01.C/D/G consumes the active
feature to install the private `__sloppy.time` V8 bridge for native delay and monotonic
deadline accounting. CORE-TIME-01.H aligns filesystem stdlib options with Time concepts;
it does not add new compiler-emitted Plan metadata beyond the existing `stdlib.fs` and
`stdlib.time` feature activation. Runtime feature activation treats `stdlib.time` as a
dependency of `stdlib.fs` so numeric filesystem timeouts work for apps that import only
`sloppy/fs`. CORE-TIME-01.I adds examples/conformance/golden evidence without changing
the Plan schema or activation rules. CORE-FS-01.C/D/H consumes the active feature to
install the first core `__sloppy.fs` V8 bridge and stdlib `File` wrappers.
CORE-CRYPTO-01.A/B adds the `stdlib.crypto` descriptor and compiler activation for
`sloppy/crypto`, while keeping it unavailable by default until implementations land.
CORE-CODEC-01.A/B adds the `stdlib.codec` descriptor and compiler activation for
`sloppy/codec`, while keeping it unavailable by default until Base64/Hex/Text/Binary/
Compression/Checksum implementations land.
CORE-FS-01.E/F extends the same feature-gated
bridge with Directory, FileHandle, temp, atomic, and symlink primitives, plus native
lock-file primitives under the filesystem backend contract. CORE-FS-01.G extends the same
feature with resource-backed non-recursive file/directory watch events.

## Public API Shape

The plan is not normally user-authored. It is a public compatibility contract for `sloppyc`,
`sloppy`, diagnostics tools, and future package tooling. Users may inspect artifact
metadata through `sloppy routes`, `sloppy doctor`, `sloppy audit`, and `sloppy openapi`, and
may execute EPIC-21 artifacts through `sloppy run --artifacts` in V8-enabled dev builds.
Routes, capabilities, doctor, and OpenAPI introspection validate the file with the native
Plan v1 parser before reporting metadata. Audit remains a bounded metadata checker that can
inspect deliberately invalid problem fixtures without running user code, but it returns
nonzero when it emits ERROR findings.

## Versioning Rules

- `schemaVersion` is mandatory in JSON fixtures.
- Runtime must reject plans with unsupported major schema versions.
- Runtime may accept compatible minor additions only if unknown fields are explicitly
  allowed by that schema version.
- Plan must declare runtime minimum version. Unknown optional fields are ignored in Plan
  v1. Top-level `requiredFeatures` is now parsed as required runtime-feature metadata and
  validated by the ENGINE-27 runtime feature registry before runtime initialization.
- Plan must declare target platform and target engine.
- Compiler version is diagnostic metadata and cache input.
- TASK 06.A supports only schema version `1` through `sl_plan_version_supported`.

## Implemented Minimal Plan v1 Shape

The implemented native C shape in `include/sloppy/plan.h` is deliberately small:

- `schemaVersion` / `SlPlan.version`;
- `compilerVersion` / `SlPlan.compiler_version`;
- `runtimeMinimumVersion` / `SlPlan.runtime_min_version`;
- `stdlibVersion` / `SlPlan.stdlib_version`;
- `target.platform` and `target.engine`;
- `bundle.path`, `bundle.id`, and `bundle.hash`;
- `sourceMap.path`, `sourceMap.id`, and `sourceMap.hash`;
- `handlers[].id`, `handlers[].exportName`, and `handlers[].displayName`.
- optional `routes[].method`, `routes[].pattern`, `routes[].handlerId`, and
  `routes[].name`;
- optional `dataProviders[].token`, `dataProviders[].provider`, `dataProviders[].service`,
  and `dataProviders[].capability`;
- optional `capabilities[].token`, `capabilities[].kind`, `capabilities[].access`, and
  `capabilities[].provider`.
- optional `requiredFeatures[]` / `SlPlan.required_features`;
- optional compiler metadata `configuration.environment`, `configuration.keys[]`, and
  `configuration.providers[]`. This is emitted for tooling/diagnostics and is not yet part
  of the native `SlPlan` struct.
- optional compiler metadata `modules[]`, `sourceFiles[]`, route/plan `completeness`,
  route `bindings`, `response`, `effects`, `configReads[]`, `schemas[]`, and `strongPlan`.
  These fields are emitted for Strong Plan consumers and are intentionally ignored by the
  native Plan v1 struct until typed graph consumers land.

MAIN1-10 capability rules:

- supported capability kinds are `database`, `filesystem`, and `network`;
- database access is `read`, `write`, or `readwrite`;
- filesystem access is `read`, `write`, `readwrite`, `append`, `delete`, `list`,
  `metadata`, `watch`, or `lock`;
- network skeleton access is `connect`, `listen`, or `connect-listen`;
- database capabilities require `provider` and the value must reference
  `dataProviders[].token`;
- filesystem and network skeleton capabilities must not declare `provider`;
- capability tokens use the same bounded token syntax as provider tokens and must not carry
  secrets or connection strings.
- obvious secret-bearing fields such as `connectionString`, password/PWD, secret, API key,
  and access-token values are rejected in provider/capability metadata.

These are Sloppy runtime policy checks, not OS sandbox rules. CORE-FS-01.C/D/H adds
runtime policy checks for the core filesystem operations, and CORE-FS-01.E/F applies the
same checks to Directory, FileHandle, temp, atomic, symlink, and native lock operations:
project-relative and named-root paths are resolved through Slop-owned policy, named-root
traversal is rejected, development absolute paths produce warnings, and strict-mode
absolute paths are denied unless allowed. OS sandboxing remains out of scope. Network
entries remain metadata/check-only until scoped APIs exist. CORE-FS-01.G maps V8
watch open/next/close through the existing `fs.watch` capability operation and the same
path policy; recursive watch remains unsupported until the platform backend grows that
contract deliberately.

All native `SlStr` fields are borrowed views in the struct model. Manually constructed
plans borrow caller-owned storage. `sl_plan_parse_json` copies JSON strings and handler
arrays into the supplied arena, so parsed plan lifetime is tied to that arena rather than
to the JSON parser document. `sl_plan_intern_metadata` can build an arena-owned copy of a
validated Plan with selected stable metadata interned into a caller-owned bounded
`SlInternTable`. That helper leaves the input Plan untouched on failure and rolls back its
own arena allocations.

Handler rules implemented now:

- handler IDs are numeric runtime dispatch keys;
- handler ID `0` is reserved and invalid;
- duplicate handler IDs are invalid plan input;
- handler export names remain compatibility and diagnostic metadata. EPIC-24 V8 dispatch
  uses the numeric handler ID registered through the runtime intrinsic rather than a named
  global lookup;
- display names are diagnostic/user-facing only.

Unknown JSON fields are allowed and ignored in Plan v1 for forward compatibility. Known
fields with the wrong JSON type fail validation. Top-level `requiredFeatures` is the
exception to unknown-field ignore semantics: entries are parsed into `SlPlan` and then
validated against the runtime feature registry. Unknown entries fail with
`SLOPPY_E_UNKNOWN_RUNTIME_FEATURE`; known but unavailable entries fail with
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`; unavailable dependencies fail with
`SLOPPY_E_RUNTIME_FEATURE_DEPENDENCY_MISSING`. These diagnostics include the feature id and
Plan/requested-by context where the current Plan shape can provide it.

Current descriptor import mapping is intentionally narrow and mirrors what the compiler and
stdlib already understand: `sloppy/app` maps to `stdlib.framework/app`, `sloppy/results` to
`stdlib.results`, `sloppy/schema` to `stdlib.schema`, `sloppy/config` to `stdlib.config`,
`sloppy/data` to `stdlib.data`, `sloppy/time` to `stdlib.time`, `sloppy/fs` to `stdlib.fs`,
`sloppy/crypto` to `stdlib.crypto`, `sloppy/codec` to `stdlib.codec`, and
`sloppy/providers/sqlite` to `provider.sqlite`.
PostgreSQL and SQL Server provider descriptors exist as unavailable/deferred entries for
Plan validation; the crypto descriptor is active for V8 plans after CORE-CRYPTO-01.C/D/F/H
registered vetted random/hash/HMAC backends and the `__sloppy.crypto` intrinsic namespace.
The codec descriptor is known but unavailable by default until implementation PRs register
vetted codec backends and V8 intrinsics.

## Schema Sections

### metadata

Build identity and provenance:

- plan schema version;
- compiler version.

### target

Runtime compatibility:

- target platform;
- target engine.

### bundle

Executable JavaScript artifact:

- path;
- bundle ID;
- hash.

### sourceMap

Diagnostic mapping artifact:

- path;
- source map ID;
- hash.

The compiler-emitted `app.js.map` is the authoritative source-map companion for the
generated bundle. ENGINE-15.A keeps the Plan reference small and deterministic while the
map carries the richer `x_sloppy` metadata for source files, handler generated/source
positions, route/module/schema/provider/capability/effect source locations, and source
file hashes.

### modules

App module graph:

- module name;
- optional package/version;
- dependency list;
- deterministic order;
- contributed phases;
- source location.

TASK 14 adds only bootstrap debug metadata for modules. `app.__debug().modules` includes
module names, dependencies, deterministic order, capability tokens, service tokens, route
strings, and custom metadata for tests and examples. The native Plan v1 parser still does
not parse a `modules` section into `SlPlan`, but COMPILER-30.H/I now emits optional
compiler `modules[]` metadata for function modules in generated Plans.

COMPILER-30 target module metadata comes from the supported compiler module graph:
relative imports, function modules, contributed routes, providers, config/schema/result
metadata, and source locations. Runtime package loading, npm modules, controllers, and
decorators remain out of scope.

COMPILER-30.B/C establishes the compiler-side source graph foundations that later Plan
metadata will consume. The current emitted Plan shape remains compatibility-preserving:
relative imports and Slop-owned imports are resolved for compilation. COMPILER-30.H/I
adds optional top-level `modules[]` and `sourceFiles[]` metadata for Strong Plan consumers;
older native Plan v1 parsing ignores those optional fields.

### routes

Native route table input:

- method;
- path pattern;
- handler ID;
- optional name.

TASK 10.A implements the native parser/matcher for the initial route path pattern subset
that plan `routes[].pattern` values reuse. MAIN1-02 promotes the compiler-emitted route
metadata into a Plan v1 alpha section. ENGINE-02 widens method metadata to the ENGINE-01
core set. The native parser validates each route entry when the section is present:

- `method` is required and must be `GET`, `POST`, `PUT`, `PATCH`, or `DELETE`;
- `pattern` is required and must parse with the native route pattern parser;
- `handlerId` is required and must reference a declared `handlers[].id`;
- duplicate `method` + `pattern` pairs are rejected;
- duplicate non-empty route names are rejected;
- array order is preserved as source order. MAIN1-04 dev dispatch builds a native route
  table from GET route metadata only and applies the alpha precedence policy: literal
  patterns before parameter patterns, stable source order when precedence is equal.

TASK 10.C synthetic HTTP dispatch tests still build a manual borrowed table, but the
compiler/runtime artifact path now owns its GET route mapping through `app.plan.json`.
EPIC-19 CLI introspection can read route metadata from plan-compatible JSON
fixtures/artifacts. MAIN1-11 hardens the route, doctor, and OpenAPI commands so artifact
metadata is validated by the native Plan v1 parser before output is produced.
EPIC-21 compiler output uses the same plan-compatible metadata idea for extracted routes:
each route entry records `method`, `pattern`, `handlerId`, `name`, and compiler-owned
source metadata. COMPILER-30.D extends that route source to Minimal API calls, nested
literal route groups, and function-module contributions. COMPILER-30.E adds compiler-owned
metadata for supported request bindings (`routes[].bindings`), preliminary result helper
response metadata (`routes[].response`), top-level schema declarations (`schemas[]`), and
config helper reads (`configReads[]`). Function-module route entries also include the
module function name in `routes[].module`; the route `source.path` points at the
contributing source file. Handler IDs start at `1` and are assigned in source order after
route group prefix composition and module contribution expansion.
COMPILER-30.F/G adds optional route `effects[]` metadata for supported provider effects.
Each effect records capability kind, provider kind, provider token, inferred access
(`read`, `write`, or `readwrite` for the database adapter family), operation, and the
source-level reason such as `db.query`. Provider registrations created with
`app.use(sqlite(...))` narrow their emitted capability access to the merged route effects
when those effects are statically known. Builder database capability metadata can represent
SQLite, PostgreSQL, and SQL Server provider kinds even though only the SQLite-generated
runtime opener is executable today. If a route would require a generated PostgreSQL or
SQL Server provider-backed runtime wrapper, `sloppyc` rejects the source with
`SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE` instead of emitting a handler that fails at
runtime.
EPIC-22 `sloppy run` consumes those route entries for dev-only GET dispatch and validates
that each referenced handler ID exists in the parsed minimal Plan handler table before
serving requests. EPIC-23 uses the same route entries to materialize route params into the
handler request context; MAIN1-02 moves the route section validation into the native Plan
parser. ENGINE-02 broadens compiler/Plan method metadata but does not broaden route
precedence, HTTP runtime behavior, middleware, or non-GET request dispatch.

Implemented path pattern syntax is limited to `/`, static segments, `{name}`, `{name:str}`,
and `{name:int}`. Query strings are parsed from request targets by EPIC-23 request context
code, not route patterns. Literal route group prefixes compose before validation.
Catch-all parameters, optional segments, regex constraints, method matching beyond GET dev
dispatch, route precedence, middleware/filter metadata, and runtime validation remain future
work. Compiler-emitted route and plan completeness are now present for the supported
COMPILER-30.H/I subset, and `sloppy openapi` emits the supported Plan-derived OpenAPI
subset while keeping unsupported metadata explicit.

The COMPILER-30.E/H/I metadata is emitted for static tooling and Strong Plan consumers.
The native runtime parser accepts these unknown optional metadata fields while storing
top-level `requiredFeatures[]` for ENGINE-27 feature activation. ENGINE-20.C CLI consumers now read
`routes[].bindings`, `routes[].response`, `routes[].effects`, `routes[].completeness`, and
top-level `completeness` for routes, capabilities, doctor, and audit output. Emitting or
displaying this metadata does not mean runtime request validation, native JSON
optimization, route partitioning, multi-isolate execution, or broader provider/capability
enforcement has been implemented. `sloppy openapi` consumes the metadata for the supported
OpenAPI subset and marks unknown body/response shape with explicit partial extensions
instead of inventing schemas.

Dynamic route paths are invalid in the compiler-owned static path unless a future explicit
runtime-only escape hatch provides all required provider/capability metadata.

### handlers

Handler table contract:

- numeric handler ID;
- generated export or registration name;
- user-facing name.

The remaining sections below are future schema sections and are not represented in TASK
06.A native structs.

### services

Service graph declarations:

- token;
- lifetime;
- module;
- dependencies;
- source location.

### middleware

Global or group pipeline entries:

- ID;
- module;
- order;
- handler/filter reference;
- applies-to metadata.

### filters

Endpoint and result filters:

- endpoint filters for pre-handler checks;
- result filters for post-handler, pre-response transforms;
- order and scope.

### dataProviders

Database provider registrations:

- token;
- provider name;
- module;
- lifetime;
- config key references;
- required driver/package metadata;
- health check metadata.

MAIN1-02 implements a minimal metadata-only native `dataProviders` section. Provider
entries may carry:

- `token`, required;
- `provider`, required and limited to `sqlite`, `postgres`, or `sqlserver`;
- optional `service`;
- optional `capability`, which must reference `capabilities[].token` when present.

Tokens must be non-empty and may contain letters, digits, `.`, `_`, and `-`. Duplicate
provider tokens are rejected. MAIN1-03 also treats non-empty `service` values as represented
service metadata during runtime startup validation: service tokens must use the same token
syntax and duplicate provider service tokens are rejected before serving. Plan files must
reference config keys or redacted placeholders only; raw connection strings and other
secrets do not belong in plan metadata.

This section is parseable and validated metadata. MAIN1-02 does not open providers or
perform driver checks. ENGINE-06 loads the parsed `dataProviders` section into the runtime
capability registry so the V8 SQLite bridge can deny mismatched provider access before
calling SQLite. PostgreSQL and SQL Server JavaScript bridges remain deferred.

### capabilities

Capability declarations:

- token;
- kind;
- access mode;
- provider/source module;
- config key references;
- path policy where applicable.

MAIN1-02 implements the native `capabilities` section and MAIN1-10 tightens it for runtime
checks. Capability entries may carry:

- `token`, required;
- `kind`, required and limited to `database`, `filesystem`, or `network`;
- `access`, required and validated for the kind;
- required `provider` for database capabilities, which must reference
  `dataProviders[].token`;
- no `provider` for filesystem or network capabilities.

Database access values are `read`, `write`, or `readwrite`; filesystem access values are
`read`, `write`, `readwrite`, `append`, `delete`, `list`, `metadata`, `watch`, or `lock`;
network access values are `connect`, `listen`, or `connect-listen`. Duplicate capability
tokens are rejected.

Capabilities are loaded into the runtime registry and checked by token, kind, access mode,
and database provider within the V8 SQLite bridge before provider calls. CORE-FS-01.E/F
adds core, advanced, and FileHandle filesystem operations behind feature-gated V8
registration; CORE-FS-01.G adds watch resources behind `fs.watch`; CORE-FS-01.I/J adds
filesystem doctor/audit goldens and source examples that prove metadata visibility.
Network entries remain skeleton checks only and no capability entry creates an OS sandbox.

### permissions

Permission requirements:

- route/service/module reachability;
- database permissions;
- filesystem permissions;
- environment permissions;
- future native plugin permissions.

### diagnostics metadata

Data used for better errors:

- source locations;
- generated locations;
- source map links;
- user-facing names;
- suggested fix metadata when available.

### schemas

Validation and OpenAPI metadata:

- schema ID;
- kind;
- route binding usage;
- source module;
- source location;
- JSON-schema-like shape or Sloppy-specific compact representation;
- diagnostics display name.

TASK 13.C implements only the bootstrap JavaScript `schema` object shape and optional route
metadata storage. The native Plan v1 parser still does not parse a `schemas` section, route
validation binding, OpenAPI metadata, or schema references.

## File And Module Layout

Likely implementation areas:

```text
include/sloppy/plan.h
src/core/plan.c
src/core/plan_validate.c
tests/golden/plan/
tests/integration/plan/
compiler/src/plan/
```

Do not create parser or validator files without tests.

## Minimal Plan v1 Fixture

Current handwritten fixture shape:

```json
{
  "schemaVersion": 1,
  "compilerVersion": "sloppyc-placeholder",
  "runtimeMinimumVersion": "0.1.0",
  "stdlibVersion": "0.1.0",
  "target": {
    "platform": "windows-x64",
    "engine": "v8"
  },
  "bundle": {
    "path": ".sloppy/app.js",
    "id": "app-js-test",
    "hash": "test-only"
  },
  "sourceMap": {
    "path": ".sloppy/app.js.map",
    "id": "app-js-map-test",
    "hash": "test-only"
  },
  "handlers": [
    {
      "id": 1,
      "exportName": "__sloppy_handler_1",
      "displayName": "Home"
    }
  ]
}
```

## Future Full Plan

Later schema phases may add modules, services, middleware, filters, schemas, permissions,
diagnostics metadata, and build cache metadata beyond the MAIN1-02 route/provider/capability
alpha sections. ENGINE-02.E does not add Plan cache metadata; it documents cache-key
requirements and rebuilds source-input artifacts conservatively.

Illustrative future shape, not implemented:

```json
{
  "schemaVersion": 1,
  "compilerVersion": "sloppyc-placeholder",
  "runtimeMinimumVersion": "0.1.0",
  "stdlibVersion": "0.1.0",
  "target": {
    "platform": "windows-x64",
    "engine": "v8"
  },
  "bundle": {
    "path": ".sloppy/app.js",
    "id": "app-js-placeholder",
    "hash": "sha256-placeholder"
  },
  "sourceMap": {
    "path": ".sloppy/app.js.map",
    "id": "app-js-map-placeholder",
    "hash": "sha256-placeholder"
  },
  "handlers": [
    {
      "id": 100,
      "exportName": "__sloppy_handler_100",
      "displayName": "Users.Get"
    }
  ],
  "routes": [],
  "modules": [],
  "services": [],
  "dataProviders": []
}
```

## Validation Rules

Current TASK 06.B parser validation checks, with TASK 06.C golden fixture coverage:

- JSON root is an object;
- `schemaVersion` exists and equals supported Plan v1;
- `compilerVersion`, `runtimeMinimumVersion`, and `stdlibVersion` exist and are non-empty
  strings;
- `target.platform` and `target.engine` exist and are non-empty strings;
- `bundle.path`, `bundle.id`, and `bundle.hash` exist and are non-empty strings;
- `sourceMap.path`, `sourceMap.id`, and `sourceMap.hash` exist and are non-empty strings;
- `handlers` exists and is a non-empty array;
- every handler has nonzero unsigned integer `id`;
- handler IDs are unique;
- every handler has non-empty string `exportName` and `displayName`;
- when `routes` is present, it is an array of GET/POST/PUT/PATCH/DELETE route entries with
  valid native route patterns, valid handler references, unique method/pattern pairs, and
  unique non-empty names;
- when `dataProviders` is present, it is an array with valid unique tokens and supported
  provider values (`sqlite`, `postgres`, `sqlserver`);
- when `capabilities` is present, it is an array with valid unique tokens, supported kinds,
  supported access values for each kind, required database provider references, and no
  provider references on filesystem/network skeleton entries;
- malformed JSON fails with a diagnostic rather than crashing.

Current TASK 06.B parser validation deliberately does not check target compatibility,
runtime minimum version compatibility, stdlib availability, bundle hash contents, or source
map hash contents. MAIN1-02 checks runtime compatibility and artifact hashes in the
supported `sloppy run --artifacts` execution path where artifact bytes are available.
MAIN1-03 then validates the parsed app graph before serving: supported target/runtime
values, handler table presence, duplicate handler IDs, at least one runnable route,
route-to-handler references, duplicate route method/pattern pairs, duplicate route names,
provider/capability metadata consistency, and duplicate represented service tokens.

Native `SlPlan` module metadata is not represented in the parsed struct yet. Bootstrap
module debug metadata remains JavaScript-only, while COMPILER-30.H/I compiler artifacts may
emit optional top-level `modules[]` metadata that the Plan v1 parser ignores for runtime
startup validation.

Future plan validation must check:

- schema version is supported;
- target platform and engine are supported;
- required features are known;
- bundle and source map fields are present;
- module dependency graph has no cycles;
- module order is deterministic;
- handler IDs are unique;
- every route handler ID exists;
- every expected handler export/registration name is present during startup;
- route ambiguity is rejected before serving;
- service tokens are unique within allowed scopes;
- data provider tokens match services/capabilities;
- permissions reference known capabilities;
- schema references point to known schemas;
- request/response schemas are emitted only after a scoped compiler/runtime task produces
  real schema references;
- unsupported dynamic mode is rejected unless explicitly enabled;
- source map presence follows target mode policy;
- no secret values are embedded in plan config.

TASK 06.A provided helper coverage for supported version checks, handler ID validity,
handler lookup, and duplicate handler ID detection. TASK 06.B adds minimal JSON parser
validation on top of those helpers.

## Runtime Compatibility Rules

Runtime rejects a plan when:

- runtime version is below `runtimeMinimumVersion`;
- target engine is not available;
- target platform is incompatible;
- required stdlib version is unavailable;
- required feature is unknown;
- bundle hash does not match;
- expected handler table is not registered.

MAIN1-02 implements the supported-path subset for `sloppy run --artifacts`: schema version
1, `target.platform == "windows-x64"`, `target.engine == "v8"`, and
`runtimeMinimumVersion == "0.1.0"` are required today. The run path reads the referenced
`bundle.path` and `sourceMap.path`, rejects missing files, and verifies `sha256:` hashes
before evaluating bootstrap or user JavaScript. These hashes detect artifact drift between
plan and files; they are not a security trust boundary and do not replace signing or
package provenance. Bundle and source-map paths must remain relative artifact paths; the
loader rejects absolute paths, drive-qualified paths, `.`/`..` components, and paths that
cannot fit in its bounded path builder.

## Compiler Emission Rules

`sloppyc` must:

- emit deterministic JSON ordering where practical;
- include source locations for user-facing declarations;
- include stable numeric handler IDs;
- avoid secret values;
- emit source map links;
- validate static plan restrictions before writing success artifacts;
- produce golden-testable output.

MAIN1-02 compiler output emits deterministic `sha256:` hashes for `app.js` and
`app.js.map`, keeps artifact paths relative, and emits the hardened route section.
ENGINE-02 adds `handlers[].async`, compiler-owned `source` metadata under handlers/routes,
top-level `features`, minimal SQLite `dataProviders`/database `capabilities`, and a real
handler-line source map. Unsupported source must fail before `app.plan.json`, `app.js`, or
`app.js.map` success artifacts are written.
ENGINE-15.A adds source locations to compiler-emitted provider/capability Plan entries and
expands the generated source map with stable Sloppy metadata. The runtime still validates
presence and hashes; runtime exception remapping remains a later diagnostics consumer.

## Error And Diagnostic Behavior

Plan diagnostics should identify:

- failing section;
- offending token/route/module/handler;
- source location when available;
- machine-readable diagnostic code;
- suggested fix when safe.

Examples:

- duplicate handler ID;
- route references missing handler;
- module dependency cycle;
- missing config key for provider;
- incompatible runtime feature.

Current TASK 06.B parser diagnostics are intentionally smaller:

- `SL_DIAG_MALFORMED_JSON`;
- `SL_DIAG_INVALID_PLAN_VERSION`;
- `SL_DIAG_INVALID_PLAN_FIELD`;
- `SL_DIAG_DUPLICATE_HANDLER_ID`.

Diagnostics include error severity, stable code, message, optional source name, and a simple
hint. JSON pointer spans, source frames, source maps, and rendered code frames are deferred.

## Testing Requirements

Plan tests should include:

- valid minimal plan;
- valid plan with multiple handlers;
- unknown future-field allowance;
- malformed JSON;
- unsupported schema version;
- missing required top-level field;
- missing bundle;
- missing bundle path;
- missing source map;
- missing handlers;
- empty handlers;
- invalid handler ID;
- duplicate handler ID;
- missing handler export;
- empty handler export;
- wrong known-field type.

Future parser/validator phases should add:

- route missing handler;
- module cycle;
- invalid provider token;
- permissions referencing unknown capability;
- golden full plan fixture.

TASK 06.C parser tests read the checked-in fixtures under `tests/golden/plan/` directly.
The fixture matrix covers valid minimal plans, multiple handlers, unknown future-field
allowance, malformed JSON, unsupported versions, missing required sections and fields,
empty handlers, handler ID `0`, duplicate handler IDs, missing or empty handler
`exportName`, and wrong known-field types. Wrong handler ID type is also covered by an
embedded parser input because the checked-in fixture matrix uses handler ID `0` as the named
invalid handler ID case.

## Quality Gates

- plan fixture tests run in CTest;
- golden plan fixtures are stable;
- malformed plans produce diagnostics, not crashes;
- no secrets appear in checked-in plan fixtures;
- plan schema changes update this document and ADRs when architectural.

## Implementation Tasks

- Define C structs for parsed plan model.
- Add minimal plan fixture directory.
- Choose JSON parser in the appropriate phase. Done for TASK 06.B with `yyjson`.
- Add validator with section-specific diagnostics. Started for minimal v1 shape.
- Add handler table extraction. Done for minimal v1 handlers.
- Add plan/bundle consistency checks.
- Add golden tests for JSON fixtures.
- Add CTest integration for valid and invalid plans.
- Add schema section fixtures before validation API lands.
- Add dynamic mode rejection fixture.

## Acceptance Criteria For Plan v1 Loader

Plan v1 loader is accepted when:

- runtime can load a minimal handwritten plan;
- validator rejects malformed required sections;
- handler table is exposed to later runtime code;
- diagnostics identify section and source location where available;
- tests cover success and failure fixtures;
- no V8, HTTP, compiler extraction, or database provider code is required.

## Open Questions

- Exact numeric ID allocation strategy.
- Whether plan JSON permits comments in dev mode.
- Whether unknown fields are warnings or errors in v1.
- Exact source location representation for generated module contributions.
## ENGINE-14 Module Metadata

ENGINE-14 does not add a top-level native `modules` Plan section yet, but compiler-emitted
routes may carry module attribution and module source locations. Function-module routes
and provider/capability metadata are emitted into the same Plan route/provider/capability
graph as direct app registration. The generated source map may list multiple author files
so diagnostics can point at module files where the current runtime can use source-map
metadata.
