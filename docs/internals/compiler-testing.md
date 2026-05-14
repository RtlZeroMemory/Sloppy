# Compiler Testing Strategy

Sloppy compiler tests are layered so a wrong artifact cannot pass only because
a golden was updated.

## Test Layers

- Focused Rust unit tests cover extractor and emitter rules close to the
  module that owns them.
- Semantic compiler-contract tests validate emitted `app.plan.json`,
  `routes.slrt`, and dependency graph metadata with
  `compiler/src/compiler_contract.rs`.
- Regression seeds under `compiler/tests/regressions/` replay historical bug
  shapes through the compiler and validator.
- Deterministic grammar cases in `compiler_contract_validation` generate small
  supported apps from a bounded model and compare output against the expected
  semantic model.
- `tests/dogfood/compiler-mega-app` is a checked-in large compiler app that
  exercises route groups, modules, provider effects, static assets, config,
  package imports, helper chains, shadowing, OpenAPI metadata, and common HTTP
  methods.
- Alpha/template/source-input lanes continue to prove CLI and packaged workflow
  behavior. They should run semantic validation before golden comparison when
  they emit compiler artifacts.
- Longer fuzz/property lanes are optional or release-oriented unless a PR
  changes the compiler logic they cover.

## PR Gates

Fast compiler contract lane:

```powershell
cargo test --manifest-path compiler/Cargo.toml --test compiler_contract_validation
```

Full compiler lane:

```powershell
tools/windows/test-engine.ps1 -Area compiler -Tier pr
```

V8-backed alpha/template/source-input evidence:

```powershell
tools/windows/test-engine.ps1 -Area v8 -Tier pr
tools/windows/test-engine.ps1 -Area alpha-flow -Tier pr
tools/windows/test-engine.ps1 -Area templates -Tier pr
```

The Windows test engine uses `tools/windows/dev.ps1 configure -Preset
windows-relwithdebinfo -EnableV8`, which resolves or fetches the pinned SDK
before it runs V8-backed CTest lanes. Do not mark those lanes unavailable only
because `SLOPPY_V8_ROOT` is unset.

Unix equivalent:

```sh
tools/unix/test-engine.sh --area compiler --tier pr
```

The compiler area includes the dedicated `compiler.contract` lane plus the
broader compiler Cargo tests and relevant CTest fixtures.

## Adding A Historical Seed

1. Create `compiler/tests/regressions/<bug-name>/case.json`.
2. Add the smallest source tree that reproduces the supported or rejected
   shape.
3. Record semantic expectations such as route count, provider-effect route
   count, native no-JS endpoint count, package dependency presence, or expected
   diagnostic code.
4. Run `cargo test --manifest-path compiler/Cargo.toml --test compiler_contract_validation`.

Do not add fixtures that are never executed. If a bug cannot run yet, add the
smallest honest static/semantic check and document the runtime gap.

## Choosing Coverage

Use an invariant when a behavior spans artifacts or could be hidden by a
golden diff. Use a focused unit test when one extractor or emitter owns the
rule. Use a regression seed when the bug shape is historically important. Use
the grammar generator when the same semantic model should survive syntax
variations. Use the dogfood app when the risk comes from feature interaction
across modules, helpers, packages, and generated artifacts.

Use a golden only after the semantic rule is already protected, or for
human-facing output where exact bytes are the contract.
