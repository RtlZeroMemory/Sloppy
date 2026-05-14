# Agent Contribution Rules

This file is for automation agents working in this repository. Human contributor
instructions live in `CONTRIBUTING.md`.

## Documentation

Write documentation for human readers. Match the directory layout under
`docs/`:

- `docs/install.md`, `docs/quickstart.md` — onboarding.
- `docs/tutorials/` — guided lessons through first-time workflows.
- `docs/api/` — first-party TypeScript API reference.
- `docs/cli/` — `sloppy` and `sloppyc` commands.
- `docs/guide/` — task-shaped walkthroughs.
- `docs/reference/` — exhaustive lookup material.
- `docs/about/` — design notes and background.
- `docs/contributor/` — repository operations.
- `docs/internals/` — implementation invariants.

Do not add visible machine metadata lines to the top of pages. Do not paste
prompts, planning transcripts, or agent choreography into docs or PR bodies.
Delete stale planning material unless the user explicitly asks for a historical
archive.

## Evidence Lane Report

Use only these statuses in PR evidence tables: `PASS`, `FAIL`, `SKIPPED`,
`UNAVAILABLE`, `DEFERRED`, and `NOT RUN`.

Report skipped optional gates under their own status. A default non-V8 pass is
separate from V8, package, live-provider, stress, sanitizer, fuzz, and
benchmark lanes.

When V8 matters, resolve the SDK before declaring the lane unavailable. On
Windows, `.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo
-EnableV8` uses the repo resolver and can discover or fetch the pinned SDK;
`SLOPPY_V8_ROOT` is only an override. The test-engine V8, alpha-flow, golden,
integration, examples, and template lanes should use that SDK-backed preset.
Report V8 as `UNAVAILABLE` only after the resolver/configure step fails, and
include the exact failure.

Name each lane separately when it matters:

- default non-V8
- compiler/Plan
- V8-gated
- source-input
- package outside-checkout
- platform-specific
- dependency-backed
- live-network/live-provider
- advanced static analysis
- fuzz/property
- stress/torture
- sanitizer/memory-safety
- benchmark

Benchmark output is measurement data only. It is never correctness coverage.

## PR Bodies

Keep PR bodies reviewable. Include the implementation contract, source docs,
non-goals, validation commands, skipped or unavailable lanes, and deferred
coverage. Do not include prompts, hidden reasoning, or stale task planning text.

## Boundary

Keep agent- or LLM-specific policy in `AGENTS.md` and this file only. Do not
copy agent-only instructions into contributor docs under `docs/contributor/*`.
