# Compiler Standards

These standards apply to the Rust compiler and tooling code under `compiler/`.
They refine the general [Rust standards](../contributor/rust-standards.md) for
the `sloppyc` extraction, Program Mode, Plan emission, generated JavaScript,
and diagnostics paths.

## Module Ownership

Keep compiler files small enough to review by responsibility.

- `sloppyc.rs` is the orchestration and CLI-facing compatibility entrypoint.
  It should parse options, call extractors and emitters, and return artifacts
  or diagnostics.
- Feature extraction belongs in focused modules such as route metadata, auth,
  health, static files, services, data, FFI, and stdlib imports.
- Program Mode graph traversal, import analysis, module transforms, generated
  wrappers, and emitted JavaScript belong in Program Mode or emit modules.
- Plan writers belong near Plan emission, split by metadata area when a writer
  becomes hard to scan.
- Do not create empty placeholder modules. A module should own real behavior.
- Do not replace one giant file with another generic junk drawer.

New compiler features should start in a dedicated module once the behavior has
more than a small local helper. Large files must be split by responsibility
before adding adjacent behavior.

## Extractor Honesty

The compiler must not claim metadata it cannot prove.

- Static extractors must use AST matching for semantic source shapes. Small
  lexical scans are allowed for bounded preflight or generated-output helpers,
  but not as substitutes for AST ownership.
- Dynamic but runnable web source should emit partial or dynamic metadata and
  findings when the generated JavaScript can still execute.
- Unsupported static-required declarations, unsafe FFI shapes, invalid artifact
  shapes, unsupported runtime dependencies, unresolved source, or source that
  cannot be transformed must fail with a diagnostic.
- Never silently omit routes, handlers, auth, schema, provider, config,
  capability, or dependency metadata to make a build look successful.
- Keep provider and runtime bridge metadata honest. Do not emit executable
  bridge claims for a runtime surface that is not available.

## Diagnostics

Compiler diagnostics are part of the contract.

- Use stable `SLOPPYC_E_*` and `SLOPPYC_W_*` codes.
- Include the best available path, span, and hint.
- Wording must be deterministic and specific to the rejected shape.
- Hints must name a supported shape or explain the exact unsupported boundary.
- Do not panic on user-controlled source, config, package metadata, paths, or
  Plan input.
- Malformed metadata and unsupported dynamic shapes should use consistent
  diagnostic constructors where a shared helper improves clarity.

## Rust Style

Compiler Rust should be direct and boring.

- Prefer small focused functions over long nested match arms.
- Move module-specific parser or extractor logic into that module.
- Prefer typed structs and enums over ad-hoc JSON or string maps while
  extracting; convert to JSON at the emission boundary.
- Avoid needless cloning, repeated string construction in loops, and avoidable
  allocation in hot compiler passes when the clearer borrowed form is local.
- Do not add broad `unwrap()`, `expect()`, `panic!`, `todo!`, or
  `unimplemented!` on user-controlled paths.
- Keep common helpers narrow. A helper is shared only when at least two
  responsibilities really use the same rule.

## Tests And Goldens

Tests must pin intended compiler behavior rather than accidental output.

- Every extractor feature needs focused unit or fixture coverage for supported
  and rejected shapes.
- Every Program Mode wrapper or transform needs smoke coverage that proves the
  emitted artifact remains runnable.
- Every Plan metadata change needs golden or semantic Plan coverage.
- Unsupported dynamic shapes need tests that prove the diagnostic or partial
  finding is honest.
- Route metadata tests should cover fluent chain order, group inheritance, auth
  merging, schema metadata, and dynamic fallback behavior.
- Do not delete tests or goldens to make a refactor easier.

When goldens change, the PR must explain why the diff is intentional.

## Generated Output

Generated artifacts must stay deterministic.

- Preserve stable ordering for routes, handlers, dependency graph entries,
  required features, diagnostics, source maps, and package manifests.
- Do not leak secrets into generated Plans, diagnostics, source maps, or CLI
  output.
- Keep generated JavaScript wrapper formatting stable and covered by tests.
- Embedded stdlib, provider, service, route, and Program Mode wrappers must be
  tested when moved or changed.

## Dependencies

Do not add compiler dependencies casually.

- Use existing Oxc, serde, and local helper modules when they fit.
- A new crate needs a clear reason, a scoped use, and PR-body justification.
- Do not migrate to a different parser or code generator without a dedicated
  compiler-design task.
