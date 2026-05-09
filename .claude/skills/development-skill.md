# Development Skill

Use this for bounded implementation tasks.

1. Read `AGENTS.md`.
2. Read the source docs, ADRs, and current GitHub issue state that govern the work.
3. Classify the task: runtime, compiler, stdlib, docs, tooling, public docs, examples, or
   release evidence.
4. Identify invariants, non-goals, user-facing docs, module docs, architecture docs, ADRs,
   skills, examples, and tests that may need updates.
5. For complex work, write an `Implementation Contract for Reviewers`: source docs,
   intended behavior, non-goals, files/surfaces touched, negative paths, evidence lanes,
   and deferred coverage.
6. Implement the smallest coherent bounded slice. Large PRs are acceptable when they cover
   one bounded context; avoid both micro-PR paralysis and unrelated rewrites.
7. Add or update tests/checks from documented intended behavior.
8. Update docs when behavior, APIs, diagnostics, architecture, tests, examples, or workflow
   changes.
9. Never update expected outputs merely to match current code without explaining the intent
   change.
10. Before adding an abstraction, ask which source doc requires it, what invariant it
    enforces now, and whether direct code would be clearer.
11. Add comments for ownership, invariants, platform/engine boundaries, async/threading
    assumptions, and non-obvious safety checks.
12. Do not add comments that narrate syntax.
13. Do not make public alpha, production, performance, package, provider, V8, or
    Node/Bun/Deno compatibility claims unless the source doc and evidence lane prove them.
14. Use targeted subagents or specialist reviewers for high-risk sweeps.
15. Run applicable gates and report commands, skipped lanes, unavailable lanes, and results
    honestly.
