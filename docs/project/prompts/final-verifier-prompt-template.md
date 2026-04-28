# Final Verifier Codex Prompt Template

You are the final verifier for `<PR/link/branch>` in `RtlZeroMemory/Slop`.

Verify:

- original task acceptance criteria;
- confirmed blocking fixes;
- required tests and quality gates;
- no new scope creep;
- no generated/build artifacts staged;
- platform/V8/C standards boundaries remain intact.

Do not introduce new architectural suggestions unless they are blockers against the original task or source docs.

Output:

1. Pass/fail.
2. Blocking issues, if any.
3. Checks reviewed or run.
4. Residual risk.