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
recognized framework features that need explicit AppGraph representation before
they can produce strong metadata. `src/graph.rs` owns the internal AppGraph
data types copied out of parser lifetimes. `src/plan_emit.rs` consumes that
graph for Plan JSON so the Plan shape is separated from extraction.

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

Sloppy does not require every route to be statically understood. If the
compiler can emit runnable JavaScript, the app can run. Static source gives
stronger Plan metadata; dynamic source produces partial metadata and findings.
The compiler must not reject runnable JavaScript only because metadata is
incomplete. Fatal diagnostics are reserved for execution impossibility, invalid
artifact shape, unsafe static-required declarations such as FFI, unsupported
runtime features, or source that cannot be resolved or transformed.

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

Program Mode parses the entry and supported module graph with Oxc. It resolves
relative modules, npm specifiers for installed pure-JavaScript packages from
`node_modules`, a package.json subset, CommonJS/JSON modules, string-literal
dynamic import() calls, computed dynamic import() calls over `moduleInclude`
graphs, and the explicit Node compatibility registry. It rejects remote
imports, native addons, unsupported package export shapes, unsupported Node
builtins, and unsupported Sloppy provider imports with diagnostics. It
preserves opaque metadata instead of claiming route or OpenAPI structure. It
uses Oxc transform support to strip supported TypeScript syntax, then rewrites
supported module syntax into the generated artifact bundle. The emitted entry
wrapper supplies Program arguments/context, installs the Program console while
top-level code and the entrypoint run, and converts numeric returns into CLI
exit codes.

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
- Unsupported runtime dependencies, invalid artifact shapes, and source that
  cannot be transformed into runnable JavaScript fail with stable
  `SLOPPYC_E_*` diagnostics instead of being silently ignored.
- Metadata incompleteness is not by itself a fatal error when the generated
  JavaScript remains runnable. Emit partial/dynamic Plan metadata and honest
  dependency graph entries instead.
- Dynamic web route shapes that remain runnable emit `SLOPPYC_W_DYNAMIC_ROUTE`
  findings and partial/dynamic Plan metadata instead of fatal diagnostics.
- Generated provider bridges remain honest about runtime support. Static
  non-SQLite provider handles fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`.
- Compiler performance evidence comes from local benchmark reports and timing
  JSON, not from public claims or benchmark smoke tests.

## Failure Behavior

Compiler failures return a source-located diagnostic when source context is
available. Common failures include unsupported imports, missing packages,
unsupported package export shapes, native addons, unsupported Node builtins,
dynamic import in web extraction paths, invalid known route methods,
unsupported runtime-only handler shapes, invalid route metadata options,
missing relative imports, and provider bridge gaps.

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

- Package-manager install, registry access, semver solving, and lockfile policy
  are outside `sloppyc`. The compiler resolves what is already installed.
- Arbitrary TypeScript type checking is outside `sloppyc`; TypeScript tooling
  remains responsible for that.
- Program Mode is not full Node compatibility. It supports bundled compatible
  JavaScript modules, Sloppy stdlib imports that the runtime exposes, explicit
  partial Node compatibility shims, generated `main(args, ctx)`/default/top-level
  entrypoint execution, Program console stdout/stderr, and numeric exit codes
  when V8 is enabled.
- Middleware, CORS, RequestId, RequestLogging, and controller mapping execute in
  the bootstrap app-host path today. The compiler recognizes those source
  surfaces and rejects them with specific diagnostics instead of silently
  ignoring them until AppGraph, Plan, and generated artifacts can represent the
  behavior honestly.
- Controller constructor injection and broader response-writing APIs are still
  future compiler work.
