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

`src/sloppyc.rs` still owns most extraction. `src/graph.rs` owns the internal
AppGraph data types copied out of parser lifetimes. `src/plan_emit.rs` consumes
that graph for Plan JSON so the Plan shape is separated from extraction.

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
capabilities, configuration reads, runtime feature requirements, and source
spans.

Route metadata includes literal route names, literal route option tags, and
tags inherited from `app.group(...).withTags(...)`.

## Lifecycle

`sloppyc build <input> --out <dir>` loads the entry source, resolves supported
relative imports, extracts the AppGraph, applies configuration metadata, and
writes artifacts into the requested output directory.

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
- `compiler/tests/compiler_fixture_harness.rs`
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

## Current Limits

- npm and `node_modules` resolution are outside the current source graph.
- Arbitrary TypeScript type checking is outside `sloppyc`; TypeScript tooling
  remains responsible for that.
- Middleware, CORS, health, and request logging execute in the bootstrap
  app-host path today. Plan metadata for those surfaces needs a compiler/runtime
  slice that can represent emitted artifacts honestly.
- Controller constructor injection and broader response-writing APIs are still
  future compiler work.
