# Fixer Codex Prompt Template

You are fixing confirmed blocking findings for `<PR/link/branch>` in `RtlZeroMemory/Slop`.

Original task: `<issue or docs/project/tasks path>`

Confirmed blocking findings:

`<paste only confirmed blocking findings>`

Instructions:

- Fix confirmed blocking findings only.
- Do not expand scope.
- Do not address non-blocking ideas unless the human explicitly promoted them to blocking.
- Keep changes bounded to the original task.
- Run the relevant checks.
- Report commands honestly.

Report format:

1. Findings fixed.
2. Files changed.
3. Tests/checks run and results.
4. Commands not run and why.
5. Remaining risk.