# Sloppy Agent Guide

## Mission

Sloppy is a pre-alpha TypeScript backend runtime/app-host with a C runtime
kernel, an isolated C++ V8 bridge, and a Rust `sloppyc` compiler.

This file and `AGENTS_CONTRIBUTING.md` define agent-only rules. Keep human
workflow guidance in `CONTRIBUTING.md` and `docs/contributor/*`.

## Operating Rules

- Read this file and `AGENTS_CONTRIBUTING.md` before editing.
- Read the relevant source docs before implementation.
- Keep each change scoped to one coherent contract.
- Use GitHub issues for live roadmap/task state.
- Do not paste prompts, hidden reasoning, or choreography text into docs,
  commits, comments, or PR bodies.
- Use execution plans only when the task needs durable multi-step tracking.
- Keep docs, tests, and checks aligned with behavior changes.
- Prefer mechanical checks over memory.
- Report commands honestly, including failures and commands not run.
- Delete stale docs and planning residue by default. Archive only when there is
  durable historical value.

## Documentation Contract

Current docs must come from current code/tests/scripts/examples and commands
actually run. Old docs are discovery input, not validation.

Use Diataxis structure by reader need:

- `tutorials`: guided learning with a working result.
- `how-to`: one concrete task and exact steps.
- `reference`: precise lookup.
- `explanation`: design reasoning and mental model.
- `contributor`: operational contributor workflows.
- `internals`: boundaries, invariants, lifecycle.

Do not add fake examples, dry status pages, or unsupported
readiness/performance status statements.

## Evidence Lane Contract

PR evidence tables must use only:

- `PASS`
- `FAIL`
- `SKIPPED`
- `UNAVAILABLE`
- `DEFERRED`
- `NOT RUN`

Report skipped optional gates under their own status.

## Implementation Contract for Reviewers

Implementation PRs must include expected behavior under test, source-of-truth
contract, explicit non-goals, negative paths, lanes run, lanes skipped or
unavailable, goldens changed and why, secret/redaction checks, and deferred
coverage.

Tests should fail on contract violations rather than mirror accidental current
output.
