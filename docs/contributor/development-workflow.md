# Development workflow

How to ship a change.

## Local loop

```powershell
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

That's the inner loop. `configure` is one-time per preset; the rest are
per-change.

For runtime/compiler/V8-adjacent work, also run the V8-enabled lane:

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

See [building-from-source.md](building-from-source.md) for the full
list of commands.

## PR shape

One coherent change per PR. Examples of good shape:

- "core bytes view overflow guards + tests"
- "provider redaction coverage + docs"
- "CORS middleware contract (no behavior yet)"

Examples of bad shape:

- "core primitives, diagnostics, router, and compiler cleanup"
- "fix everything CodeRabbit complained about"

A bounded change is easier to review and easier to revert. Big
behavior shifts are still fine — they just need a coherent narrative,
not a diff that grew accidentally.

When you change behavior, move the docs and tests with it. A PR that
changes API behavior without updating `docs/api/<area>.md` will get
review feedback to add the doc change before merging.

## Tests

Add or update tests for the slice you're changing. The existing test
surface lives in `tests/`; pick the lane that matches what you're
proving:

| Lane                        | Where                                  |
| --------------------------- | -------------------------------------- |
| Unit                        | `tests/unit/<area>/`                   |
| Integration                 | `tests/integration/<area>/`            |
| Conformance (CTest fixtures)| `tests/conformance/`                   |
| Compiler fixtures           | `compiler/tests/fixtures/`             |
| Source-input end-to-end     | `tests/fixtures/source-input/`         |
| Goldens (rendered output)   | `tests/golden/`                        |

See [testing.md](testing.md) for the principles and
[testing-inventory.md](testing-inventory.md) for the layout.

If a change is genuinely test-irrelevant (a doc fix, a renaming-only
refactor that doesn't touch behavior), say so explicitly in the PR
body so reviewers don't have to ask.

## Review focus

What reviewers look for:

- **Correctness.** Does the code do what the PR says it does?
- **Boundaries.** Does it respect the platform/V8/JS boundaries
  ([architecture.md](../internals/architecture.md))?
- **Memory safety.** Ownership, bounds, overflow. The C-side
  [memory model](../internals/memory-model.md) is the contract.
- **Cleanup.** Every resource has an owner and a cleanup path.
- **Diagnostics.** Failures produce structured `SlDiag`s with stable
  codes and useful hints.
- **Scope.** No drive-by refactors that double the diff.

The longer version is in [review-playbook.md](review-playbook.md).

## Follow-ups

If you spot something real but out of scope, file a follow-up issue
and link it from the PR. Don't bury fixes in unrelated diffs, and
don't punt required tests or safety fixes to a later PR.

## Generated artifacts

Don't commit them. The build outputs are:

- `build/`
- `compiler/target/`
- `target/` (top-level Cargo cache, if any)
- `.sdeps/` (dependency cache)
- `.sloppy/` (per-app build output)
- Local V8 SDKs

These are all in `.gitignore`. Double-check before pushing — `git
status` is your friend.

## Before opening a PR

1. `dev.ps1 build` and `dev.ps1 test` pass.
2. `dev.ps1 lint` and `format-check` pass.
3. V8-enabled lane passes if your change is runtime/compiler-adjacent.
4. Docs updated where behavior changed.
5. `git status` looks clean.
6. The PR body explains *what* and *why*; the diff explains *how*.
