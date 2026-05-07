# Agent Skills

Skills are small playbooks for common agent tasks. They are not the source of truth; they
point agents back to source docs, ADRs, checks, and execution plans. Use the relevant skill
before starting work.

Shared rules for every skill:

- identify source docs and live issues first;
- keep the PR bounded to one coherent context;
- write an `Implementation Contract for Reviewers` for large or high-risk work;
- report evidence lanes honestly;
- keep docs, tests, and code together when intent changes;
- do not claim public alpha, production readiness, performance, package readiness,
  provider readiness, V8 readiness, or Node/Bun/Deno compatibility without source docs and
  matching evidence;
- use targeted subagents or independent reviewers for high-risk sweeps, not as process
  ceremony for trivial changes;
- avoid speculative architecture and stale construction wording.

## Skills

- [Development skill](development-skill.md): use for bounded implementation tasks.
- [Review skill](review-skill.md): use for PR or patch review.
- [C safety skill](c-safety-skill.md): use for C runtime code.
- [JS/TS stdlib skill](js-ts-stdlib-skill.md): use for bootstrap stdlib, examples, and
  public JS/TS API shape.
- [Rust compiler skill](rust-compiler-skill.md): use for `compiler/`, `sloppyc`, Rust
  fixtures, and Rust-owned tooling.
- [Docs to issues skill](docs-to-issues-skill.md): use when turning docs into GitHub
  epics/tasks.
- [Platform boundary skill](platform-boundary-skill.md): use for platform-sensitive
  changes.
- [Diagnostics skill](diagnostics-skill.md): use for diagnostics design or implementation.
- [Build tooling skill](build-tooling-skill.md): use for CMake, scripts, CI, and gates.
