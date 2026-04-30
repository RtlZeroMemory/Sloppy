# Strategic System Audit

Status: evidence-backed planning audit, not a feature implementation.

Audit date: 2026-04-29.

This file records separate audit passes across the engine/framework foundation. The goal is
to define what must be finished before higher-level product features, public alpha docs, or
benchmark claims.

## Evidence Anchors

- Compiler and Plan: `docs/compiler.md`, `docs/compiler-supported-syntax.md`,
  `docs/app-plan.md`, `include/sloppy/plan.h`, `src/core/plan_parse.c`,
  `compiler/src/sloppyc.rs`.
- Runtime/V8/HTTP: `docs/execution-model.md`, `docs/concurrency.md`,
  `docs/modules/http/README.md`, `include/sloppy/engine.h`, `src/engine/v8/*`,
  `src/core/http*.c`, `src/main.c`.
- Data/security/resource: `docs/data-providers.md`, `docs/security-permissions.md`,
  `include/sloppy/resource.h`, `src/core/resource.c`, `src/data/sqlite.c`,
  `src/engine/v8/intrinsics_sqlite.cc`.
- Conformance/evidence: `tests/conformance/README.md`, `CMakeLists.txt`,
  `.github/workflows/ci.yml`, `tools/windows/package.ps1`, `benchmarks/README.md`,
  `docs/quality-score.md`.

## Audit Passes

### 1. Compiler

Current state: real Oxc-backed `sloppyc build` exists for a supported single-file
JavaScript app shape. It emits deterministic `app.plan.json`, `app.js`, and `app.js.map`
artifacts with stable handler IDs and `sha256:` hashes.

What works: one `.js/.mjs` source, public `"sloppy"` import, one app, literal
GET/POST/PUT/PATCH/DELETE route metadata, simple route groups, inline handlers, direct
async handler metadata, supported `Results.*` descriptors, minimal SQLite
provider/capability metadata, deterministic artifacts, real handler-line source maps, and
clear rejection for many unsupported shapes.

Skeletal: source maps are not yet consumed by runtime diagnostics; the compiler is a
single-file extractor, not a general JS/TS compiler or bundler.

Deferred: broader async handler source shapes, non-GET request dispatch, named handlers,
imports beyond the Sloppy facade, modules/services/schema extraction, broad provider graph
extraction, TypeScript checking/lowering, source-input `sloppy run`.

Misleading risk: older compiler planning sections still read like implementation has not
started; current docs must separate "MVP implemented" from "final compiler pipeline not
complete."

Must complete for engine foundation: source-input handoff policy, runtime consumption of
async/non-GET/provider metadata, rejected shape diagnostics as syntax grows, and
deterministic artifact metadata.

Can postpone: broad TypeScript type checking, arbitrary import graphs, npm/package
resolution, bundling, dynamic route registration, package-manager behavior.

### 2. Plan / Artifact Contract

Current state: native Plan v1 parser and structs include target, bundle, source map,
handlers, routes, providers, and capabilities. `sloppy run --artifacts` validates artifact
paths and hashes before V8 execution.

What works: parser fixtures cover required fields, handler lookup, route metadata,
provider/capability sections, duplicates, malformed fields, and artifact hash checks.

Skeletal: module/service graph metadata is still not a complete runtime app graph; source
maps are validated as artifacts but not used for real remapping.

Deferred: full route/provider/capability emission from compiler source, future OpenAPI
metadata, optimization metadata, richer diagnostics/source mapping.

Misleading risk: treating Plan presence as proof that all framework concepts are wired.
Some sections can be parsed before the compiler emits or runtime consumes them fully.

Must complete for engine foundation: Plan as source-of-truth for route table, handler table,
capabilities, providers, artifact hashes, diagnostics/source mapping, compatibility checks,
and future static validation.

Can postpone: dynamic runtime discovery, broad app graph optimization, full OpenAPI schema
generation.

### 3. Runtime App Host

Current state: artifact-path runtime loads `app.plan.json`, validates startup metadata,
checks hashes, loads bootstrap/app artifacts in V8 when enabled, registers handlers, and
can run a dev-only socket/`--once` path.

What works: startup validation, route table construction, handler registration validation,
minimal request cleanup boundary, `--once` conformance hooks.

Skeletal: no full native module/service app graph, DI container, activation lifecycle,
public `app.run/listen`, provider opening lifecycle, or async request retention.

Deferred: source-input run handoff, request/app scopes with provider resources, disposal
hooks, graceful shutdown policy for pending async work.

Misleading risk: calling the dev host a production server. It is framework runtime
foundation, not a Kestrel/Nginx replacement.

Must complete for engine foundation: startup/shutdown lifecycle, app/request scopes,
resource cleanup, lifecycle diagnostics, V8/pending async shutdown policy.

Can postpone: hot reload, multi-process scaling, production deployment supervisor.

### 4. V8 Engine

Current state: optional V8 bridge is real and gated. It owns isolate/context state, loads
classic generated artifacts, installs bootstrap runtime support and SQLite intrinsics, calls
registered handlers, converts result descriptors, and maps some exceptions.

What works: V8 SDK validation, owner-thread checks, handler registration, generated-source
exception diagnostics, request-context calls, SQLite intrinsic bridge.

Skeletal: ESM module loading and module cache are not final; compiler source maps are not
consumed for author-source remaps; ENGINE-03 Promise support is limited to returned handler
Promises that settle during the explicit owner-thread microtask drain.

Deferred: true ESM/bootstrap module graph, native async completion queues, async
stack/source-remap policy, global platform teardown decision, packaged V8 runtime execution
evidence.

Misleading risk: default non-V8 gates do not validate V8 execution.

Must complete for engine foundation: isolate/context ownership, app load/evaluation,
bootstrap runtime, module/cache strategy, exception/source-map mapping, Promise/microtask
integration, shutdown and resource cleanup.

Can postpone: inspector/debugger, multi-isolate scaling, production-grade module loader
features not needed by supported Sloppy apps.

### 5. Async / Promise / Event Loop Model

Current state: native `SlLoop`, `SlAsync`, and inline worker-pool skeletons exist. ENGINE-03
settles JavaScript handler Promises that complete during the V8 owner-thread microtask
drain and fails rejected or still-pending Promises deterministically.

What works: deterministic native completion queue and settlement contracts.

Skeletal: no native async provider queue, no timers/fetch event loop, no real worker
threads, no thread-safe posting, and no cross-turn request-scope retention beyond the
bounded microtask drain.

Deferred but foundation-required: richer cancellation-token propagation,
deadline/timeout/shutdown hooks, bounded native queues, provider-backed backpressure,
async database strategy, and public timer APIs if they are ever scoped.

Misleading risk: "microtask-only async handler support" is not a Node-style event loop or
native provider async scheduler.

Must complete for engine foundation: native completion queue integration, owner-thread
continuation dispatch for native completions, request cancellation propagation, deadline
hooks, bounded completion queues, provider-backed rejection diagnostics, request-scope
lifetime until native async settlement, cleanup on cancellation/error.

Can postpone: broad public timer APIs, advanced provider-specific mid-operation
interruption, multicore scaling.

### 6. HTTP Runtime

Current state: native parser, route matcher, route table, GET dispatch, query parsing,
request-context materialization, response writer, and safe dev error responses exist for the
current artifact path.

What works: complete-buffer request-head parsing, supported method token parsing, GET route
dispatch, literal-before-parameter precedence, route/query params, `Results.text/json` style
response serialization, unsupported body rejection.

Skeletal: only GET routes are currently runnable through compiler/runtime dispatch; body
parsing and headers in JS context are not implemented.

Deferred but foundation-required: POST/PUT/PATCH/DELETE route dispatch, JSON/text body
policy, header/body limits, request cancellation signal, timeout hooks, bounded
request/response queues, and backpressure diagnostics. Multipart/file upload, middleware,
streaming, and production hardening remain later scope.

Misleading risk: response writer/request context exist, but not the full framework HTTP
runtime.

Must complete for engine foundation: API method set, route precedence,
params/query/headers, cancellation signal, body policy, bounded resource limits, result
serialization, error contract, localhost lifecycle, production boundary.

Can postpone: TLS, HTTP/2, compression, file upload, static files, production reverse-proxy
hardening.

### 7. JS/TS Framework API

Current state: bootstrap stdlib exposes `Sloppy`, `Results`, `schema`, `data`, app/builder,
route groups, modules, services, config, logging, and examples as API-shape behavior. Only
the narrow compiler-supported shape is executable through artifacts.

What works: frozen descriptors, route metadata, result helpers, static/Node-based bootstrap
shape tests, compiler facade import rewrite for the supported subset.

Skeletal: rich bootstrap examples are not generally compiler/runtime examples.

Deferred: service/module registration as runtime contract, app.run/listen, typed request
binding, validation responses, middleware/filters.

Misleading risk: examples under `examples/` can look runnable even when their README says
they are static API-shape examples.

Must complete for engine foundation: final core app API, request context, Results API, data
API, async policy, and clear split between engine core and later framework perks.

Can postpone: route-group ergonomics beyond core, validation schema breadth, advanced DI,
filters, middleware, module packages.

### 8. SQLite Bridge

Current state: native SQLite provider is solid and a V8-gated SQLite JS bridge exists. The
bridge uses resource IDs and supports open/close/exec/query/queryOne.

What works: native `:memory:` tests, primitive parameter binding, transactions at native
provider level, JS wrapper with bridge-unavailable behavior outside V8, V8-gated bridge
fixture.

Skeletal: JavaScript bridge does not yet call the native capability policy hook; public
source example is not yet a full compiler/runtime SQLite app.

Deferred: prepared-statement decision, file DB capability policy, app/request-scope
connection lifecycle, async offload, transaction callback semantics through JS bridge.

Misleading risk: "SQLite bridge exists" is not "permission-enforced SQLite data story."

Must complete for engine foundation: public JS API, native bridge, resource ownership,
capability requirement, query/exec/queryOne, transaction/prepared-statement policy,
`:memory:` and file DB policy, conformance.

Can postpone: ORM, migrations, query builder, advanced type mapping.

### 9. Capability / Security System

Current state: capability metadata, Plan/native registry, provider-policy check hooks, and
metadata audit fixtures exist. This is not an OS sandbox.

What works: capability/provider validation, denied/missing/insufficient policy checks where
callers use the hook, redaction helpers.

Skeletal: SQLite JS bridge does not yet enforce the policy hook; filesystem/network
capabilities are metadata/check skeletons.

Deferred: permission prompts, OS sandboxing, filesystem/network APIs, live audit.

Misleading risk: documenting "security" without saying there is no OS sandbox and no JS
bridge enforcement yet.

Must complete for engine foundation: capability registry, enforcement at SQLite bridge
open/use, denied diagnostics, audit/doctor checks, redaction.

Can postpone: OS sandbox research, permission prompts, network/filesystem API breadth.

### 10. Resource Lifecycle

Current state: generation-checked resource table and JS-safe resource IDs exist; SQLite
bridge uses resource IDs instead of raw pointers.

What works: kind/generation validation, stale/closed diagnostics, cleanup callbacks,
engine-owned table disposal.

Skeletal: request/app scope ownership for provider resources is not complete.

Deferred: leak reports where possible, provider pool/transaction/request cleanup, async
pending-resource retention.

Misleading risk: resource table is not full app lifecycle.

Must complete for engine foundation: request scope, app scope, deterministic cleanup,
shutdown diagnostics, lifecycle tests.

Can postpone: generalized plugin resource registries beyond known provider/resource kinds.

### 11. Diagnostics / Source Maps

Current state: stable diagnostics, text/JSON rendering, source-frame rendering, redaction,
compiler diagnostic fixtures, V8 generated-source diagnostics, and CLI fixture outputs
exist.

What works: deterministic code/severity/message output for implemented paths.

Skeletal: compiler source maps have handler-line mappings, but runtime diagnostics do not
consume them for author-source remapping.

Deferred: async stack/error policy, CLI-wide diagnostic format plumbing, structured fixes,
localization.

Misleading risk: validating `app.js.map` presence/hash or compiler map emission is not the
same as runtime source-map remapping.

Must complete for engine foundation: compiler/runtime/V8 diagnostics, source frames, real
source maps, JSON diagnostics, redaction, async rejection mapping.

Can postpone: localization, IDE protocols, rich fix suggestions.

### 12. CLI Introspection

Current state: `routes`, `doctor`, `audit`, and `openapi` inspect Plan-compatible metadata.
They do not compile, start handlers, run V8, or check live providers by default.

What works: deterministic golden text/JSON outputs, redaction, route skeleton OpenAPI.

Skeletal: OpenAPI is route skeleton only; doctor/audit are metadata/evidence-aware, not
live runtime verification.

Deferred: live provider checks behind flags, real schema/security OpenAPI, source-map-aware
diagnostics.

Misleading risk: route skeleton OpenAPI is not full OpenAPI generation.

Must complete for engine foundation: CLI surfaces for evidence, diagnostics, routes,
capabilities, package smoke, and conformance status.

Can postpone: public API explorer, full schema generation, live machine audit by default.

### 13. Conformance / Examples

Current state: conformance suite compiles supported examples by default and runs selected
fixtures only when V8 is enabled. Most source-stdlib examples are API-shape only.

What works: hello/request-context compile artifacts, unsupported dynamic/import rejection,
V8-gated hello/request-context/result/SQLite bridge runs.

Skeletal: body/header/async handler conformance is absent because those features are not
implemented.

Deferred: realistic API app examples using JSON body and SQLite through public source.

Misleading risk: public examples must not be promoted until executable through the real
toolchain.

Must complete for engine foundation: hello, request context, async handler, JSON body,
SQLite users API, denied capability, unsupported behavior.

Can postpone: large sample apps, tutorials for PostgreSQL/SQL Server, benchmarks in docs.

### 14. Packaging / Distribution

Current state: experimental Windows ZIP and Unix TAR package tooling exists, with local
outside-checkout smoke. V8 runtime packaging validation is limited unless a V8 execution
smoke is run.

What works: package layout, manifest/checksum, stdlib asset inclusion, exclusion of
generated artifacts, CLI help/version smoke.

Skeletal: package smoke is not public release readiness.

Deferred: hosted package CI, V8 package execution, dynamic runtime dependencies, signing,
notarization, installers, package manager distribution.

Misleading risk: package smoke does not prove V8 execution, live providers, or release
quality.

Must complete for engine foundation: packaged local runtime works outside checkout for the
supported compiler/runtime examples.

Can postpone: installers, signing/notarization, auto-update, package registries.

### 15. CI / Gates

Current state: default non-V8 CI runs across Windows/Linux/macOS with CMake/Cargo/scanners.
Optional V8 workflow is manual/gated.

What works: default build/test/lint/Cargo health, non-V8 failure-mode checks, cross-platform
portable C gates.

Skeletal: V8, live providers, package smoke, sanitizers, fuzzing, and performance gates are
not default required gates.

Deferred: hosted V8 SDK caching, V8 package smoke, live service CI, sanitizer/fuzz matrix.

Misleading risk: default CI green is not V8 success.

Must complete for engine foundation: evidence gates that explicitly report default, V8,
package, provider, conformance, and benchmark status.

Can postpone: performance regression dashboards, mandatory live provider CI.

### 16. Benchmarks

Current state: benchmark harness and smoke/list checks exist for foundations. Docs say
manual/local and no performance claims.

What works: route/parser/handler/synthetic dispatch benchmark harness starts and emits JSON.

Skeletal: no real HTTP throughput, V8 timing, JSON serialization, DB benchmark, trend gate,
or comparable methodology.

Deferred: external comparisons and public benchmark claims.

Misleading risk: smoke/list checks are harness evidence, not performance evidence.

Must complete before claims: release-only methodology, real paths, repeatability, hardware
context, trend tracking, external comparison criteria.

Can postpone: all benchmark marketing until engine foundation examples pass.

### 17. Public Docs Readiness

Current state: public docs are mostly honest about pre-alpha status and unsupported paths,
but README has stale wording around response writer/request context.

What works: docs distinguish default/V8/live provider/package/benchmark evidence.

Skeletal: public docs still cannot be launch docs because core engine foundation blockers
remain.

Deferred: public alpha tutorials, public SQLite tutorial, benchmark claims.

Misleading risk: public alpha docs before examples are executable.

Must complete for engine foundation: executable docs for supported workflows, honest
blocked/deferred sections, no marketing claims, no stale capability/benchmark language.

Can postpone: launch pages, marketing copy, competitor comparisons.
