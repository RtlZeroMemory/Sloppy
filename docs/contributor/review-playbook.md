# Review playbook

What reviewers look for. Use this as a checklist when reviewing or as
a self-check before requesting review.

## Correctness and contracts

- The change does what the PR description says it does.
- Behavior changes are reflected in the relevant docs and tests.
- The PR is bounded — no drive-by refactors that double the diff.
- For larger changes, the PR body lists the source docs/contracts the
  change implements and the non-goals.

## Boundaries

- **Platform.** No OS APIs outside `src/platform/`. No OS headers in
  core code. No `#ifdef` for OS branches outside platform code. See
  [internals/platform-boundaries.md](../internals/platform-boundaries.md).
- **V8.** No V8 types outside `src/engine/v8/`. JS never receives a
  raw native pointer.
- **Engine bridge invariants.** Owner-thread checks, copy-across,
  bounded microtask drain. See
  [internals/v8-bridge.md](../internals/v8-bridge.md).
- **Provider isolation.** Native pointers never leak; redaction
  preserved.

## Memory safety (C-side)

- Ownership documented at API boundaries (borrowed view, arena copy,
  scope-bound, resource-table).
- Checked arithmetic for sizes and offsets (`include/sloppy/checked_math.h`).
- No `malloc`/`free` outside allocator modules.
- No VLAs.
- Cleanups registered on the right scope; called once.
- Late completions only do cleanup; never settle JS state.

The contract: [internals/memory-model.md](../internals/memory-model.md).

## Tests

- Tests assert documented intent, not "current output".
- Negative paths covered for any contract that can reject input or
  clean up resources.
- Goldens have an explicit reason for any change.
- Optional lanes reported separately.
- New behavior has at least one test that fails when the contract
  is violated.

## Diagnostics

- Failures produce structured `SlDiag` with stable codes and useful
  hints.
- Secret-bearing values redacted from diagnostics.
- Source locations preserved where applicable.

## Build and tooling

- CMake, Cargo, and CTest updated when relevant.
- `lint`, `format-check`, and standards scanners pass.
- No generated artifacts staged.

## Simplicity

The PR does the scoped task directly. Common over-engineering smells:

- New global registry, plugin point, or vtable for one implementation.
- Macro DSL added "for future use".
- Public API expanded for hypothetical callers.
- Generic helper with no tests and one caller.
- Wrapper layer that just renames the underlying API.

If you can delete an abstraction without making the code worse, do it.

## Comments

- Public APIs document ownership and lifetime.
- Non-obvious safety checks have a one-line rationale.
- Platform/V8/threading boundaries are commented at the boundary.
- Stale comments are removed or updated.
- TODOs link to a concrete issue or include enough context to act on.

Comments that restate obvious code are removed.

## Public API and ergonomics (for stdlib, examples, and tooling)

- Public examples use `Sloppy.create()` / `app.<verb>` / `Results.*` —
  not internal helpers.
- Diagnostics are actionable.
- No Node globals, no npm assumptions.
- No descriptor shape drift (frozen objects stay frozen).
- Compiler-extractable examples avoid dynamic patterns.

## Compiler / Rust-side checks

- Output deterministic and path-normalized.
- No `unwrap()` / `expect()` / `panic!` / `todo!` / `unimplemented!` /
  `dbg!` in production code without an explicit allow reason.
- Diagnostics carry source context.
- Golden updates intentional and explained.
- `cargo fmt`, `cargo clippy -D warnings`, `cargo test` pass.

## Blocking vs non-blocking

Block on:

- correctness bug;
- memory safety risk;
- platform/V8 boundary violation;
- missing required tests;
- broken build or failed gate;
- scope creep;
- speculative abstraction that obscures ownership/error paths;
- stale or missing comment on an ownership/safety/threading rationale.

Don't block on:

- naming preference;
- optional refactor;
- style nit not covered by a standard;
- "I would have done it differently".

Non-blocking findings that point at real issues become follow-up
issues — not requirements for this PR.

## Final sweep before approval

1. Diff matches the PR description.
2. No unrelated changes.
3. Required tests added or explained as N/A.
4. Docs updated.
5. CI green for required lanes.
6. PR body still accurate after the last revision.
