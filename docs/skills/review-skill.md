# Review Skill

Use this for PR or patch review.

1. Inspect the prompt or issue.
2. Inspect relevant docs and ADRs.
3. Check scope against the requested task.
4. Check acceptance criteria.
5. Verify docs freshness for behavior, APIs, diagnostics, architecture, and module changes.
6. Verify tests map to documented intended behavior.
7. Ask for the source doc/spec when test intent is unclear.
8. Flag tests that only mirror current implementation without protecting intended behavior.
9. Review for unnecessary abstraction and speculative generality.
10. Flag "AI ceremony" as blocking when it obscures safety, ownership, or scope.
11. Review comment quality.
12. Flag stale or noisy comments.
13. Require rationale comments for non-obvious safety or lifetime behavior.
14. Separate blocking from non-blocking feedback.
15. Produce concise, actionable findings with file/line references where possible.
