## Scope

- Related EPIC/task issue:
- Bounded context:
- Out of scope:

## Summary

Describe the coherent building block this PR delivers.

## Implementation Contract for Reviewers

Reviewers must compare this PR against the task contract, not only local code quality. For
large PRs, list the expected behavior that must have landed, the source-of-truth docs or
issues, and the non-goals that must remain deferred.

## Test Intent

- Expected behavior under test:
- Source-of-truth contract:
- Positive paths:
- Negative paths:
- Goldens changed and why the output is intended:
- Secrets/redaction checks:
- Known deferred coverage:

## Evidence lanes

Use PASS, FAIL, SKIPPED, UNAVAILABLE, DEFERRED, or NOT RUN. Skipped optional gates are not pass claims. Benchmark evidence is never correctness evidence.

| Lane | Status | Commands or reason |
| --- | --- | --- |
| default non-V8 | NOT RUN | |
| compiler/Plan | NOT RUN | |
| V8-gated | NOT RUN | |
| source-input | NOT RUN | |
| package outside-checkout | NOT RUN | |
| platform-specific | NOT RUN | |
| dependency-backed | NOT RUN | |
| live-network/live-provider | NOT RUN | |
| advanced static analysis | NOT RUN | |
| fuzz/property | NOT RUN | |
| stress/torture | NOT RUN | |
| sanitizer/memory-safety | NOT RUN | |
| benchmark | NOT RUN | |

## Skipped or unavailable lanes

List each skipped, unavailable, deferred, or not-run lane with the exact reason. Do not
describe a skipped optional lane as pass evidence.

## Validation

List commands run and results. Do not mark a command as passed if it was not run.

## Checklist

- [ ] This PR maps to a documented EPIC/task.
- [ ] Scope is bounded and coherent.
- [ ] Source docs were read.
- [ ] Docs/ADRs updated if architecture changed.
- [ ] I checked whether user-facing docs, module docs, architecture docs, or ADRs need updates.
- [ ] If docs were not updated, this PR explains why.
- [ ] Tests verify documented intended behavior, not accidental current behavior.
- [ ] New behavior is backed by contract or source-of-truth tests.
- [ ] Negative paths are covered or explicitly deferred with an issue.
- [ ] Goldens changed only because intended behavior changed.
- [ ] Goldens are semantic, normalized, and redacted.
- [ ] Evidence lanes and skipped/unavailable lanes are reported honestly.
- [ ] Optional V8/package/live-provider/advanced-static/fuzz/stress/sanitizer/benchmark lanes are separate.
- [ ] No generated/build artifacts staged.
- [ ] No real secrets in tests, docs, examples, or goldens.
- [ ] No public alpha, production-readiness, or benchmark/performance claims.
- [ ] No OS APIs outside `src/platform/*`.
- [ ] No OS-specific headers outside platform directories.
- [ ] No V8 types outside `src/engine/v8/*`.
- [ ] No raw native pointers exposed to JavaScript.
- [ ] No raw `malloc`/`free` outside allocator modules once allocator exists.
- [ ] No package-manager scope.
- [ ] No Node compatibility assumptions.
- [ ] No future-phase product implementation unless explicitly scoped.
- [ ] Platform-specific tooling was placed under the correct `tools/<platform>/` directory.
- [ ] Cross-platform behavior was documented or explicitly deferred.
- [ ] Acceptance criteria for the touched task are met or explicitly deferred.
- [ ] Reviewer should classify findings as blocking/non-blocking.
