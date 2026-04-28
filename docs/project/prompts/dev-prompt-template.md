# Dev Codex Prompt Template

You are working in `RtlZeroMemory/Slop` on branch `<branch>`.

Issue/task: `<link or docs/project/tasks/... path>`

Read before editing:

- AGENTS.md
- CONTRIBUTING.md
- `<task source docs>`
- Relevant ADRs under `adr/`

Goal:

`<copy task goal>`

Scope:

`<copy task scope>`

Non-goals:

`<copy task non-goals>`

Implementation requirements:

`<copy task implementation requirements>`

Tests required:

`<copy task tests required>`

Documentation updates:

- user-facing docs: `<docs/public path or none, with reason>`;
- module docs: `<docs/modules path or none, with reason>`;
- architecture docs: `<docs path or none, with reason>`;
- ADRs: `<adr path or none, with reason>`.

Test intent:

- source doc/spec: `<doc section>`;
- intended behavior: `<behavior the tests protect>`;
- positive cases: `<list>`;
- negative cases: `<list>`.

Quality gates / commands to run:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
```

Only run compiler commands when compiler files changed or the task requires full gates.

Report format:

1. Files changed.
2. Behavior implemented.
3. Documentation updated or why not needed.
4. Tests added/updated and intended behavior covered.
5. Commands run and results.
6. Commands not run and why.
7. Remaining risks or follow-ups.

Do not claim commands passed if you did not run them. Do not expand scope beyond this task. Do not implement future-phase runtime features unless explicitly listed above.
