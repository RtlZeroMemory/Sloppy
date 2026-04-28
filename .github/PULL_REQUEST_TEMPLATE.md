## Scope

- Related EPIC/task issue:
- Bounded context:
- Out of scope:

## Summary

Describe the coherent building block this PR delivers.

## Validation

List commands run and results. Do not mark a command as passed if it was not run.

## Checklist

- [ ] This PR maps to a documented EPIC/task.
- [ ] Scope is bounded and coherent.
- [ ] Source docs were read.
- [ ] Docs/ADRs updated if architecture changed.
- [ ] I checked whether user-facing docs need updates.
- [ ] I checked whether module docs need updates.
- [ ] I checked whether architecture docs or ADRs need updates.
- [ ] If docs were not updated, this PR explains why.
- [ ] Tests added or reason documented.
- [ ] Tests verify documented intended behavior, not accidental current behavior.
- [ ] Golden outputs were updated only because intended behavior changed.
- [ ] This PR avoids speculative abstractions and future-only extension points.
- [ ] New abstractions are justified by a documented boundary, repeated real use case, or safety invariant.
- [ ] Ownership and error paths remain locally understandable.
- [ ] Public/internal APIs document ownership and lifetime where applicable.
- [ ] Non-obvious safety, platform, engine, or threading assumptions are commented.
- [ ] Comments explain rationale/invariants, not obvious syntax.
- [ ] No stale comments contradict the code.
- [ ] Windows workflow checked or reason documented.
- [ ] `format-check` passed or reason documented.
- [ ] `lint` passed or reason documented.
- [ ] No generated/build artifacts staged.
- [ ] No OS APIs outside `src/platform/*`.
- [ ] No OS-specific headers outside platform directories.
- [ ] No V8 types outside `src/engine/v8/*`.
- [ ] No raw native pointers exposed to JS.
- [ ] No raw `malloc`/`free` outside allocator modules once allocator exists.
- [ ] No package-manager scope.
- [ ] No Node compatibility assumptions.
- [ ] No future-phase implementation unless explicitly scoped.
- [ ] Platform-specific tooling was placed under the correct `tools/<platform>/` directory.
- [ ] Cross-platform behavior was documented or explicitly deferred.
- [ ] Acceptance criteria for the touched task are met or explicitly deferred.
- [ ] Reviewer should classify findings as blocking/non-blocking.
