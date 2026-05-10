# Compiler

## Purpose

`sloppyc` is the Rust compiler for Sloppy source input. It parses the supported
JavaScript and TypeScript source subset, extracts Sloppy application metadata,
and writes deterministic artifacts:

- `app.plan.json`
- generated `app.js`
- `app.js.map`

The compiler validates source shape and metadata. It does not run the program
and does not type-check arbitrary TypeScript.

## Where It Lives

```text
compiler/
  Cargo.toml
  src/
    main.rs                  CLI entry
    lib.rs                   library exports
    graph.rs                 compiler-owned AppGraph IR
    plan_emit.rs             app.plan.json emission from AppGraph
    hash.rs / version.rs     artifact hash and version constants
    sloppyc.rs               extraction plus JS/source-map emission
    sloppyc/
      configuration.rs
      schema.rs
      effects.rs
      framework_features.rs
    parser.rs                Oxc parser setup
    resolver.rs              import resolution
    module_graph.rs          source graph model
    framework_runtime.rs     framework typed metadata helpers
    static_eval.rs           bounded literal evaluation
    diagnostic.rs            compiler diagnostics
    source.rs / source_map.rs
    validation.rs            Plan completeness helpers
  tests/
    fixtures/
    compiler_fixture_harness.rs
```

`src/sloppyc.rs` still owns most extraction. The files under `src/sloppyc/`
hold focused extraction helpers for configuration, schema, effects, and
recognized framework features that must fail closed until AppGraph can represent
them. `src/graph.rs` owns the internal AppGraph data types copied out of parser
lifetimes. `src/plan_emit.rs` consumes that graph for Plan JSON so the Plan
shape is separated from extraction.

## Main Concepts

The compiler pipeline is:

```text
source file
  -> parser.rs
  -> resolver.rs / module_graph.rs
  -> sloppyc.rs extraction
  -> graph.rs AppGraph
  -> validation.rs completeness
  -> plan_emit.rs app.plan.json
  -> sloppyc.rs app.js and app.js.map
```

AppGraph records the compiler-owned view of the app: source files, modules,
routes, handlers, bindings, response metadata, schemas, providers,
capabilities, configuration reads, health endpoint metadata, runtime feature
requirements, and source spans.

Route metadata includes literal route names, literal route option tags, tags
inherited from `app.group(...).withTags(...)`, and compiler-extracted health
endpoint kind/check names from `app.mapHealthChecks(...)`.

Generated typed-handler wrappers read `Config<"KEY">` from the environment
first. If AppGraph recorded a literal default for the same key, the wrapper uses
that default instead of throwing for a missing environment value.

## Lifecycle

`sloppyc build <input> --out <dir>` loads the entry source, resolves supported
relative imports, extracts the AppGraph, applies configuration metadata, and
writes artifacts into the requested output directory. `--kind web` forces the
web app extractor. `--kind program` emits a route-free Program Mode Plan and a
generated `__sloppy_program_main` entrypoint. Without `--kind`, direct source
input tries the web app extractor first, reports an ambiguous-source diagnostic
for Sloppy web imports that do not form a web app, and otherwise falls back to
Program Mode.

`sloppyc build <input> --out <dir> --timings-json <file>` writes a structured
timing report for compiler-performance work. The alias
`--diagnostics-timing-json <file>` writes the same report for diagnostic tooling
call sites. The report records phase timings, source counters, and artifact
sizes without changing normal compiler output. Timing collection is opt-in and
intended for local benchmark evidence.

Import validation is file-local. The entry file must import `Sloppy` from
`"sloppy"`. A file imports `Results` only when handlers in that same file call
`Results.*`; registering a child function module does not require the entry file
to import `Results`.

Program Mode still parses the entry and supported relative modules with Oxc and
rejects dynamic imports, bare npm/Node imports, remote imports, and unsupported
Sloppy provider imports. It preserves opaque metadata instead of claiming route
or OpenAPI structure. It uses Oxc transform support to strip supported
TypeScript syntax, then rewrites supported static ESM imports/exports into the
generated artifact bundle.

`sloppy build` and source-input `sloppy run` invoke `sloppyc` as a separate
process. The native CLI/runtime consume only the emitted artifacts and command
results; they do not link to the compiler library.

## Invariants

- Same source and compiler version produce a deterministic Plan and
  byte-for-byte stable artifacts.
- Handler IDs are assigned from source order starting at `1`.
- Plan emission reads from AppGraph, not from parser lifetimes.
- Source locations are preserved for route, handler, schema, provider, binding,
  and effect metadata where the compiler can identify them.
- Unsupported source shapes fail with stable `SLOPPYC_E_*` diagnostics instead
  of being silently ignored.
- Generated provider bridges remain honest about runtime support. Static
  non-SQLite provider handles fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
- Compiler performance evidence comes from local benchmark reports and timing
  JSON, not from public claims or benchmark smoke tests.

## Failure Behavior

Compiler failures return a source-located diagnostic when source context is
available. Common failures include unsupported imports, npm specifiers,
dynamic import(), dynamic route patterns, unsupported route methods,
unsupported handler shapes, invalid route metadata options, missing relative
imports, and provider bridge gaps.

Rejected builds do not emit success artifacts.

## Public API Relationship

The public contract is the emitted Plan and generated artifacts. AppGraph is an
internal compiler model and is not a public API.

The Plan feeds native validation, `sloppy routes`, `sloppy audit`, `sloppy
doctor`, `sloppy openapi`, runtime feature activation, and V8 artifact
execution when V8 is enabled.

## Tests And Evidence

Compiler evidence lives in:

- unit tests in `compiler/src/sloppyc_tests.rs`
- fixture inputs and expected artifacts under `compiler/tests/fixtures/`
- `compiler/tests/fixtures/full-framework-app-graph/` for the broad supported
  AppGraph extraction contract
- `compiler/tests/compiler_fixture_harness.rs`
- `compiler/tests/compiler_scale_smoke.rs` for the medium scale smoke guard
- native Plan/CLI/runtime tests under `tests/`

Use these gates for compiler changes:

```powershell
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
tools/windows/check-rust-standards.ps1
tools/windows/dev.ps1 test
```

Run the full repository gates when compiler output or CLI metadata changes.
For compiler performance work, also run the benchmark workflow documented in
`docs/contributor/compiler-performance.md`.

## Current Limits

- npm and `node_modules` resolution are outside the current source graph.
- Arbitrary TypeScript type checking is outside `sloppyc`; TypeScript tooling
  remains responsible for that.
- Program Mode is not Node compatibility. It supports the current static import
  subset, Sloppy stdlib imports that the runtime exposes, and generated
  `main`/default entrypoint execution when V8 is enabled. Program CLI
  arguments and a context object are deferred.
- Middleware, CORS, RequestId, RequestLogging, and controller mapping execute in
  the bootstrap app-host path today. The compiler recognizes those source
  surfaces and rejects them with specific diagnostics instead of silently
  ignoring them until AppGraph, Plan, and generated artifacts can represent the
  behavior honestly.
- Controller constructor injection and broader response-writing APIs are still
  future compiler work.
