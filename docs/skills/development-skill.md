# Development Skill

Use this for bounded implementation tasks.

1. Read `AGENTS.md`.
2. Read the relevant source-of-truth docs and ADRs.
3. Identify the docs that govern the change.
4. Identify user-facing docs, module docs, architecture docs, and ADRs that may need updates.
5. Restate scope and non-goals before editing when the task is complex.
6. Implement the minimal coherent slice.
7. Add or update tests/checks from documented intended behavior.
8. Update docs when behavior, APIs, diagnostics, architecture, tests, or workflow changes.
9. Never update expected outputs merely to match current code without explaining the intent change.
10. Before adding an abstraction, answer:
    - What real problem does it solve now?
    - Which doc/ADR requires it?
    - What invariant does it enforce?
    - Can this be a simple direct function?
    - Is this the second real use case?
11. Add comments for ownership, invariants, platform/engine boundaries,
    async/threading assumptions, and non-obvious safety checks.
12. Do not add comments that narrate syntax.
13. If a comment becomes long because the code is too complex, simplify the code first.
14. Run applicable gates.
15. Report commands and results honestly.
