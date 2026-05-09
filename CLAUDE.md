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

Documentation is for humans who want to use Sloppy. Write pages that match how
readers will arrive at them: a landing/quickstart for new users, an API and CLI
reference for lookup, guides for tasks, and contributor/internals docs for the
people working on Sloppy itself. The directory layout under `docs/` is the
contract; follow it rather than re-inventing it per page.

Ground content in real behavior. Read the source, run the command, or check the
test before you write what something does. Mark experimental or planned
surfaces explicitly so readers know what to rely on. Avoid filler and
defensive hedging — short, accurate sentences beat paragraphs of caveats.

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
