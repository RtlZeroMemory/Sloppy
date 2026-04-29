# Review Playbook

## Spec Compliance Reviewer

Checks that the change matches the prompt, docs/ADRs, and acceptance criteria. Also checks
for scope creep and required doc updates.

## C Safety Reviewer

Checks ownership, bounds, overflow, cleanup, error paths, resource lifetime, platform
boundaries, and tests.

## Build/Tooling Reviewer

Checks CMake, scripts, CI, test integration, Windows-first/cross-platform assumptions, and
generated artifact hygiene.

## Simplicity / Overengineering Reviewer

Checks:

- Is the PR solving the scoped problem directly?
- Did it introduce abstractions not required by the task?
- Are there speculative extension points?
- Are helpers making code clearer or hiding logic?
- Are there too many options/flags/callbacks?
- Can a reviewer reason locally about ownership and errors?
- Would deleting an abstraction make the code safer/easier?

Blocking examples:

- new global registry without current use;
- unnecessary vtable/provider interface for one implementation, unless it is a documented
  architectural boundary;
- macro DSL;
- cross-module abstraction not in docs/ADR;
- public API added "for future use";
- large generic helper with no tests and one caller.

Non-blocking examples:

- function could be a little shorter;
- naming preference;
- minor helper extraction disagreement.

## Comment Quality Reviewer

Checks:

- Are public APIs documented with ownership/lifetime?
- Are non-obvious safety checks explained?
- Are platform/V8/threading boundaries commented?
- Are comments accurate and not stale?
- Are comments explaining necessary complexity or hiding avoidable complexity?
- Are there noisy comments that restate obvious code?
- Are TODOs linked to a task or clear follow-up?

Blocking examples:

- public API with unclear ownership/lifetime;
- non-obvious unsafe-looking code without rationale;
- stale comment contradicts implementation;
- platform/engine boundary code lacks boundary comment;
- TODO with correctness impact but no issue/task.

Non-blocking examples:

- simple private helper lacks comment but is obvious;
- minor wording improvement.

## Ergonomics/API Reviewer

Checks that public API examples remain clean, diagnostics are helpful, framework-soup drift
is avoided, and low-level primitive-first UX does not leak into user-facing surfaces unless
the code is internal.

JS/TS checklist:

- no Node globals/imports in `stdlib/sloppy/`;
- no npm or package-manager assumptions;
- descriptor shape is stable and documented;
- examples are honest about current runtime/compiler support;
- errors are deterministic and redact secrets;
- compiler-extractable examples avoid dynamic patterns;
- `tools/windows/check-js-ts-standards.ps1` passes.

Rust compiler/tooling checklist:

- output is deterministic and path-normalized;
- no `unwrap()`, `expect()`, `panic!`, `todo!`, `unimplemented!`, or `dbg!` in production
  code without explicit allow reason;
- diagnostics include source context where possible;
- golden tests are updated intentionally;
- traits, macros, and abstractions are not overbuilt;
- `tools/windows/check-rust-standards.ps1` passes;
- `cargo fmt`, `cargo clippy -D warnings`, and `cargo test` pass.

## Final Verifier

Checks only the original acceptance criteria, confirmed blocking feedback, and no new scope
creep.

## Blocking vs Non-blocking

Blocking:

- architecture violation;
- memory safety bug;
- missing required tests;
- broken build;
- failed quality gate;
- platform/V8 boundary leak;
- scope creep.
- speculative abstraction that obscures ownership, error paths, or task scope.
- stale or missing comment on required ownership, safety, platform, engine, or threading
  rationale.

Non-blocking:

- naming preference;
- future enhancement;
- optional refactor;
- style nit not covered by standards.
