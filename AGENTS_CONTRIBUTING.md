# Agent Contribution Rules

This file is for automation agents working in this repository. Human contributor
instructions live in `CONTRIBUTING.md`.

## Documentation

Write documentation for human readers. Use the directory structure under
`docs/` to keep reader goals separate:

- `docs/tutorials/` teaches guided paths.
- `docs/how-to/` solves one task.
- `docs/reference/` is lookup material.
- `docs/explanation/` explains design reasoning.
- `docs/contributor/` covers repository operations.
- `docs/internals/` records implementation invariants.

Do not add visible machine metadata lines to the top of pages. Do not paste
prompts, planning transcripts, or agent choreography into docs or PR bodies.
Delete stale planning material unless the user explicitly asks for a historical
archive.

## Evidence Lane Report

Use only these statuses in PR evidence tables: `PASS`, `FAIL`, `SKIPPED`,
`UNAVAILABLE`, `DEFERRED`, and `NOT RUN`.

Skipped optional gates are not pass claims. A default non-V8 pass is not V8,
package, live-provider, stress, sanitizer, fuzz, or benchmark evidence.

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

Benchmark evidence is measurement evidence only. It is never correctness
evidence.

## PR Bodies

Keep PR bodies reviewable. Include the implementation contract, source docs,
non-goals, validation commands, skipped or unavailable lanes, and deferred
coverage. Do not include prompts, hidden reasoning, or stale task planning text.

## Boundary

Keep agent- or LLM-specific policy in `AGENTS.md` and this file only. Do not
copy agent-only instructions into contributor docs under `docs/contributor/*`.
