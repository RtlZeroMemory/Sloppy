# Compiler Correctness Contract

This contract is normative for `sloppyc`. Goldens are receipts for emitted
bytes; they are not the source of truth for correctness.

## Supported Input Shapes

Current compiler support is the app-host source shape implemented under
`compiler/src/sloppyc*`:

- Web apps exported from `Sloppy.create()` or `Sloppy.createBuilder().build()`.
- Program Mode entries selected through the existing `--kind program` path.
- Route registration through supported literal HTTP methods, `mapGet`, route
  groups, modules, generated health routes, controllers, static files, SPA
  asset declarations, and realtime/websocket metadata where the existing
  extractors accept them.
- Handlers expressed as supported function declarations, function expressions,
  arrows, module exports, controller methods, and typed framework handlers.
- Request metadata the extractor can prove today: route params, query/body
  schema declarations, selected headers, config reads, auth, CORS, rate limit,
  middleware, services, static assets, OpenAPI route overrides, package
  dependency graph metadata, and source maps.
- Database providers declared through current capability/provider APIs. SQLite
  has the generated JS bridge path currently supported by the compiler. Other
  native providers must not be claimed as generated JS provider bridges unless
  that bridge exists.

Unsupported shapes must either fail with a stable diagnostic when execution is
impossible or emit partial/dynamic metadata with findings when the generated
JavaScript can still run.

## Extraction Honesty

The compiler must not claim metadata it cannot prove from the AST and current
resolver state. Static extractors may under-specialize to V8 execution, but
must never over-specialize to native/static execution unless equivalence is
proven.

Required honesty rules:

- Do not silently omit routes, handlers, provider effects, capabilities,
  dependency graph entries, schemas, config reads, package roots, or source-map
  references to make a build appear successful.
- A `complete`, `partial`, `dynamic`, `runtime-only`, `opaque`, or `invalid`
  status must match the evidence in the emitted Plan.
- Partial and dynamic statuses require specific reasons.
- Fatal diagnostics are reserved for unresolved source, invalid artifact shape,
  unsupported static-required declarations, unsupported runtime bridges, unsafe
  FFI shapes, or source that cannot be transformed or executed.

## Provider Effects

Provider effects are execution-affecting metadata. If a route calls `query`,
`queryOne`, `exec`, `transaction`, or another recognized provider operation,
the effect must appear in the route metadata and must resolve to a declared
capability. If it cannot resolve, the Plan must carry honest missing-provider
metadata or the compiler must fail with the existing provider diagnostic.

Routes with provider effects must not use native no-JS dispatch. This includes
native static JSON, text, empty, and problem responses. A provider-effect route
must emit:

- `dispatch.executionKind == "v8-handler"`;
- no `nativeResponse` metadata;
- no contribution to `routeDispatch.nativeNoJsEndpoints`.

## Dispatch Specialization

Native/static dispatch is an optimization. It is correct only for effect-free
deterministic handlers whose response metadata proves the emitted native body is
equivalent to the JavaScript handler result.

The compiler may choose V8 for any route. It may not choose native/static for a
route with provider effects, unresolved runtime behavior, partial response
metadata, dynamic body content, or missing proof of equivalence.

`routeDispatch.nativeNoJsEndpoints` must equal the number of routes whose
`dispatch.executionKind` is not `v8-handler`.

## Artifact Agreement

Compiler artifacts must describe the same app:

- Every route `handlerId` resolves to exactly one handler.
- Every dispatch endpoint references the same handler as the route.
- `app.plan.json` route method, pattern, execution kind, and declared artifact
  hash agree with `routes.slrt` when the route artifact is emitted.
- `app.plan.json`, emitted JavaScript, source map, dependency graph,
  route artifact, OpenAPI metadata, and runtime/package behavior must agree on
  route identity, handler identity, package roots, provider effects, response
  status, and source locations where those surfaces include the field.
- OpenAPI route overrides must not contradict extracted static response status
  metadata.
- Dependency graph package entries must identify emitted or resolvable package
  roots.

Runtime/package equivalence means source-run and packaged-run behavior should
match for the same supported input and runtime lane. If package or runtime
execution cannot run in the current lane, the PR must report that lane as
`SKIPPED`, `UNAVAILABLE`, or `DEFERRED` with a reason.

## Determinism

Given the same source tree, configuration inputs, compiler version, and options,
the compiler must emit deterministic artifacts. Stable ordering applies to
routes, handlers, required features, findings, dependency graph entries,
source-map entries, package manifests, and route artifacts.

Random, generated, and fuzz-style compiler tests must use recorded seeds. A
failure report must include the seed, fixture path, route method/pattern,
invariant name, and expected versus actual value where applicable.

## Goldens

Goldens are receipts. Updating a golden does not prove the new behavior is
correct. Golden updates must pass semantic contract validation first, and a
semantic validator failure must be fixed in the compiler or test expectation
rather than snapshotted.
