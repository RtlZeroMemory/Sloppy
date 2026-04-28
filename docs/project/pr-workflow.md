# PR Workflow

Sloppy prefers medium-sized bounded-context PRs: one coherent module, foundation slice, or workflow improvement.

A PR is too small when it creates review overhead without delivering a reviewable behavior, check, fixture, or source-of-truth update. Examples include adding one enum with no tests or adding a placeholder file unrelated to an accepted task.

A PR is too large when it mixes unrelated contexts or makes reviewer focus impossible. Examples include V8 plus HTTP plus DB work, or core primitives plus unrelated compiler expansion.

Large-coherent PRs are allowed when the work is genuinely one building block, such as core native foundation, memory foundation, diagnostics foundation, or resource lifecycle foundation. Mark them with `size:large-coherent` and consider an extra reviewer.

High-risk PRs may need a second reviewer. High-risk areas include allocator/resource lifetime, V8 boundaries, concurrency, provider dependencies, security/permissions, and diagnostics that may expose secrets.

Default workflow uses one dev Codex, one reviewer Codex, and human final review. Fixer Codex handles confirmed blocking findings. Optional final verifier confirms original acceptance criteria.

Blocking findings prevent merge. Blocking findings include architecture violations, missing required tests, broken gates, platform/V8 leakage, memory-safety concerns, scope creep, and generated artifacts staged.

Docs/test freshness is also a merge requirement. Stale docs are blocking when behavior,
APIs, diagnostics, architecture, CLI behavior, or module boundaries changed. Tests must
protect documented intended behavior; tests that only mirror accidental current behavior
should be treated as blocking when they replace required intent coverage.

Simplicity is a review requirement. Speculative abstractions, framework-like subsystems,
future-only extension points, or helpers that hide ownership/error behavior are blocking
when they make the PR harder to reason about or exceed the task scope.

Non-blocking findings are future enhancements, style preferences not covered by standards, optional refactors, or ideas that do not affect the task acceptance criteria. Convert non-blocking work into follow-up issues when it is worth tracking.
