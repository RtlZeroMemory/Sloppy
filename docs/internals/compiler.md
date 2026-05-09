# Compiler

`sloppyc` is the Rust compiler. It reads supported source, validates the
shape, and emits a deterministic Plan + JavaScript bundle. It does *not*
type-check arbitrary TypeScript and it does *not* run the program.

## Layout

```text
compiler/
  Cargo.toml
  src/
    main.rs                  CLI entry
    lib.rs
    sloppyc.rs               main extraction/emission (~6k LOC)
    sloppyc/                 helpers used by sloppyc.rs
      configuration.rs
      schema.rs
      effects.rs
    parser.rs                Oxc setup
    resolver.rs              import resolution
    module_graph.rs
    framework_runtime.rs     framework metadata extraction
    static_eval.rs           bounded literal evaluation
    diagnostic.rs
    source.rs / source_map.rs
    validation.rs
  tests/
    fixtures/                input.{js,ts} + expected plan/bundle/sourcemap/diagnostic
    sloppyc_tests.rs         driver
```

`sloppyc.rs` is large by design — extraction logic for routes, services,
providers, capabilities, schemas, and framework metadata is all close
together so the emission step can see everything at once. The supporting
modules carry shared structures and inference rules.

## Pipeline

```text
input file (.js / .mjs / .ts)
   │  parser.rs (Oxc)
   ▼
AST
   │  resolver.rs::resolve_imports
   │     allow "sloppy", "sloppy/<subpath>", relative
   │     reject npm specifiers, dynamic import(), node: prefix
   ▼
module graph
   │  sloppyc.rs::extract_app
   │     find Sloppy.create()/createBuilder()
   │     extract routes, groups, controllers, modules
   │     extract services, capabilities, providers, schemas
   │     extract framework v2 typed parameter bindings
   ▼
intermediate model
   │  validation.rs
   │     supported subset, deterministic shape
   ▼
emit
   │  sloppyc.rs::emit_*
   │     deterministic app.js (with handler wrappers)
   │     deterministic source map
   │     app.plan.json
   ▼
output to --out / cache directory
```

## Versioning

Constants in `compiler/src/sloppyc.rs`:

```rust
const COMPILER_VERSION:        &str = "sloppyc-0.x.y";
const RUNTIME_MINIMUM_VERSION: &str = "0.1.0";
const STDLIB_VERSION:          &str = "0.1.0";
```

These three appear in `app.plan.json`. The runtime checks the minimum
version on load.

## What gets extracted

- **Routes.** `app.get/post/put/patch/delete` and the `mapGet/...` aliases,
  on both `app` and group/controller mappers. Pattern is a string
  literal; method is the call name.
- **Route groups.** `app.group("/prefix")` and `app.mapGroup` form
  prefixes that combine with child patterns deterministically.
- **Controllers.** `app.controller("/prefix", ClassName, configure)` —
  the configure callback maps controller method names to routes.
- **Service registrations.** Literal
  `services.addSingleton/addScoped/addTransient("Token", factory)` calls.
- **Capabilities.** `capabilities.addDatabase("token", { ... })` and the
  inferred capability needs from typed handler parameters
  (`Sqlite<"name">`, `Postgres<"name">`, `SqlServer<"name">`,
  `WorkQueue<"name">`).
- **Schemas.** `schema.object({...})` and primitives used as `Body<T>`
  parameter types.
- **Configuration reads.** Statically visible `config.get*("KEY")` calls
  and `Config<"KEY">` typed parameters.
- **Framework v2 typed bindings.** `Route<T>`, `Query<T>`, `Body<T>`,
  `Header<"name">`, `Service<T>`, `Config<"KEY">`, provider, queue, and
  context bindings on handler parameters.
- **Visible response metadata.** Status codes from `Results.status(...)`
  and helper-inferred status codes.

## What gets rejected

- npm imports, dynamic `import()`, `node:` specifier prefix.
- Dynamic route patterns or computed method names.
- Conditional or loop-based route registration.
- Top-level `await`, `eval`, `Function` constructor.
- Arbitrary TypeScript type checking — `sloppyc` parses TS syntax but
  does not infer through arbitrary type expressions.

The full matrix lives in
[reference/supported-syntax.md](../reference/supported-syntax.md). Every
rejection produces a diagnostic with a stable code (`SLOPPYC_E_*`) and
a source location.

## Determinism

Same source + same compiler version produces the same Plan, byte-for-byte.

Determinism is enforced by:

- ordering rules (handler IDs assigned in source order from 1, route
  metadata sorted by source order at emit time);
- deterministic SHA-256 hashing of artifacts in the Plan;
- a normalized source-map line/column scheme; and
- a fixture suite (`compiler/tests/fixtures/`) that pins inputs to
  expected `app.plan.json`, `app.js`, source map, and diagnostic
  outputs. Any drift is a test failure.

## Source map

`app.js.map` is a Source Map V3 document. Mappings are produced from
`HandlerGeneratedStart` records — each handler the compiler emits has a
known generated line/column and the original span is recorded into the
mapping list.

The runtime uses this to remap exception traces back to original
sources before showing diagnostics.

## CLI

```
sloppyc build <input> --out <dir>
sloppyc --version
sloppyc --help
```

`sloppy build` is a thin wrapper: it formats CLI arguments and invokes
`sloppyc` through the platform process runner. There's no shared library
between `sloppy` and `sloppyc` — they communicate only through arguments
and artifact files.

## Tests

- **Fixtures** under `compiler/tests/fixtures/` are the canonical test
  surface. Each fixture has `input.{js,ts}` plus expected
  `app.plan.json`, `app.js`, source map, and diagnostic output. Diff
  failures fail CI.
- **Negative fixtures** assert specific `SLOPPYC_E_*` codes for
  unsupported source.
- **Unit tests** cover individual extraction paths (`schema.rs`,
  `effects.rs`, …).

## What's still moving

The framework v2 typed-handler surface (typed parameter bindings,
provider injection, schema-driven validation metadata) is the active
front. Edge cases may still fall back to a generic generated wrapper —
the fixture suite is the source of truth for what's supported.
