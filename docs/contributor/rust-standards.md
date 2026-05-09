# Rust Standards

## Purpose

Rust is used for compiler and tooling work because it fits parser, extractor, diagnostic,
and artifact-emission code. It must remain understandable to a C/.NET-heavy maintainer.
Rust cleverness is not a substitute for clear design.

Generated artifacts must be deterministic. Compiler failures must be honest and specific:
Sloppy must never silently drop routes, handlers, diagnostics, or artifact fields just to
produce something that looks successful.

## Scope

These standards govern:

- Rust code under `compiler/`;
- future `sloppyc` modules;
- Rust tests and golden harnesses;
- future Rust tooling owned by this repository.

## Rust Edition and Toolchain

`compiler/Cargo.toml` currently uses Rust edition `2021`.

Rules:

- Do not bump the Rust edition or toolchain without a documented reason.
- Keep dependencies minimal.
- Any new crate must be justified in the docs or PR body.
- Do not add parser/compiler dependencies before the scoped compiler phase needs them.

## Error Handling

- Do not use `unwrap()` or `expect()` in production code except for an impossible invariant
  with a nearby comment or an intentional process-startup hard failure.
- Tests may use `unwrap()` or `expect()` when it keeps the test focused.
- Prefer typed errors or structured diagnostic types for compiler errors.
- CLI errors should map to clear diagnostics and nonzero exit codes.
- Preserve file, path, and span context where possible.
- Do not hide parse or extraction failures behind generic "failed" messages.
- Unsupported future syntax must produce a clear diagnostic once extraction begins.

## Determinism

Compiler output must be deterministic.

Rules:

- Use stable ordering for routes, handlers, diagnostics, generated JSON, source maps, and
  artifact manifests.
- Use `BTreeMap` or sorted vectors when output order matters.
- Do not rely on `HashMap` or `HashSet` iteration order in emitted artifacts.
- Golden outputs must not contain absolute local paths unless intentionally normalized.
- Timestamps and random IDs are forbidden in deterministic artifacts unless derived from
  stable source data.

## Parser and Extractor Discipline

- AST extraction must be explicit and narrow.
- Unsupported syntax must fail clearly.
- Do not build a regex compiler for semantic extraction. Tiny preflight checks are allowed
  only when they do not replace AST understanding.
- Do not execute arbitrary JavaScript during compilation.
- Do not perform package-manager resolution.
- Do not assume Node-style runtime behavior.
- Do not use best-effort partial extraction that silently drops routes or handlers.
- Every supported syntax shape must have fixture tests.
- Every rejected syntax shape should have diagnostics tests.

## API Design

Keep compiler modules small and specific:

- `cli`;
- `diagnostics`;
- parser/extractor;
- plan writer;
- artifact writer;
- golden tests.

Rules:

- Do not create a generic compiler framework before needed.
- Do not add a plugin system.
- Do not use procedural macros.
- Do not introduce trait abstraction unless there are at least two real implementations or
  a clear testing boundary.
- Prefer simple structs and functions.

## Ownership and Allocation

- Prefer borrowed data where the lifetime is local and obvious.
- Avoid cloning large strings unnecessarily.
- Prioritize clarity and deterministic artifacts over micro-optimization in compiler code.
- Do not add an arena to the Rust compiler unless measured or justified by a scoped task.
- Avoid leaking OS-specific path behavior into golden output.

## Filesystem and Path Handling

- Normalize output paths for diagnostics and golden output.
- Avoid absolute paths in generated artifacts.
- Create output directories intentionally.
- Never delete broad directories recursively unless the path is known to be the compiler
  output directory.
- Protect against writing outside the requested output directory.
- Do not perform hidden writes outside the specified output directory.
- Do not add a global cache unless a scoped task adopts and tests it.

## Testing

- Add unit tests for extractor pieces.
- Keep golden tests current for emitted `app.plan.json`, `app.js`, and source maps.
- Add diagnostics tests for unsupported syntax.
- Treat snapshot/golden updates as intentional contract changes.
- Test names should describe intended behavior.

Rust gates:

```powershell
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
.\tools\windows\check-rust-standards.ps1
```

## Anti-Overengineering for Rust

Forbidden unless a scoped task explicitly adopts and tests the behavior:

- plugin framework;
- async runtime;
- custom build daemon;
- watch mode;
- incremental cache;
- complex trait object graph;
- macro-heavy design;
- procedural macros;
- custom JSON serializer without reason;
- global mutable compiler state;
- dependency explosion.

## Comments

Rust comments should explain:

- extraction constraints;
- why syntax is unsupported;
- deterministic ordering decisions;
- artifact compatibility;
- diagnostics policy.

Do not add comments that narrate obvious syntax.
