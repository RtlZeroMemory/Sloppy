## Scope

- Related GitHub issue:
- Bounded context:
- Out of scope:

## Summary

Describe the coherent building block this PR delivers.

## Implementation Contract for Reviewers

Reviewers must compare this PR against the task contract, source docs, and
explicit non-goals. For large PRs, list the expected behavior that landed and
the behavior that remains out of scope.

## Test Intent

- Expected behavior under test:
- Source-of-truth contract:
- Positive paths:
- Negative paths:
- Goldens changed and why the output is intended:
- Secrets/redaction checks:
- Known deferred coverage:

## Evidence Lane Report

Allowed statuses are exactly: PASS, FAIL, SKIPPED, UNAVAILABLE, DEFERRED, NOT RUN.
Skipped optional gates are not pass claims.
Benchmark evidence is never correctness evidence.

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

List each skipped, unavailable, deferred, or not-run lane with the exact reason.
Do not describe a skipped optional lane as pass evidence.

## Validation

List commands run and results. Do not mark a command as passed if it was not
run.

## Checklist

- [ ] Scope is bounded and coherent.
- [ ] Source docs were read.
- [ ] Docs/ADRs updated if architecture changed.
- [ ] I checked whether user-facing docs, reference docs, contributor docs,
      internals docs, or ADRs need updates.
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
- [ ] No public release, production-readiness, or benchmark/performance claims.
- [ ] No OS APIs outside `src/platform/*`.
- [ ] No OS-specific headers outside platform directories.
- [ ] No V8 types outside `src/engine/v8/*`.
- [ ] No raw native pointers exposed to JavaScript.
- [ ] No package-manager scope.
- [ ] No Node compatibility assumptions.
- [ ] No future-phase product implementation unless explicitly scoped.
- [ ] Platform-specific tooling was placed under the correct `tools/<platform>/` directory.
- [ ] Cross-platform behavior was documented or explicitly deferred.
- [ ] Acceptance criteria for the touched issue are met or explicitly deferred.
- [ ] Reviewer should classify findings as blocking/non-blocking.
