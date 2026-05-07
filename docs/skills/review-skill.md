# Review Skill

Use this for PR or patch review.

1. Inspect the prompt, issue, PR body, and `Implementation Contract for Reviewers`.
2. Inspect relevant source docs and ADRs.
3. Check scope against the requested bounded context and explicit non-goals.
4. Check acceptance criteria and evidence lanes.
5. Verify docs freshness for behavior, APIs, diagnostics, architecture, module docs,
   public docs, examples, and skills.
6. Verify tests map to documented intended behavior.
7. Flag tests that only mirror current implementation, update goldens without intent, or
   cover only happy paths.
8. Reject optional V8/package/live-provider/fuzz/stress/sanitizer/benchmark lanes reported
   as default pass evidence.
9. Reject benchmark smoke used as correctness or performance evidence.
10. Reject unsupported public alpha, production, performance, package-readiness,
    provider-readiness, V8-readiness, or Node/Bun/Deno compatibility claims.
11. Review for unnecessary abstraction and speculative architecture.
12. Review comment quality and flag stale breadcrumbs that should be current invariants.
13. Require rationale comments for non-obvious safety, lifetime, platform, engine, or
    threading behavior.
14. Recommend targeted subagent/specialist review only when the risk surface warrants it.
15. Separate blocking from non-blocking feedback and produce concise findings with
    file/line references where possible.
