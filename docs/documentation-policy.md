# Documentation Policy

Documentation is part of Sloppy's correctness story. A change is incomplete if
the next reader is sent to stale behavior, old task history, or unsupported
product claims.

## Source Of Truth

Current docs must be written from current source code, public headers, compiler
code, stdlib code, CLI code, scripts, tests, examples, generated artifacts, and
commands actually run.

Old docs are discovery material only. GitHub issues own live roadmap and task
state.

## Documentation Shapes

- Tutorial: guided learning with a working result.
- How-to: one concrete task with exact steps and expected output.
- Reference: lookup material for commands, APIs, config, diagnostics, and
  status.
- Explanation: architecture, design reasoning, and mental models.
- Contributor: build, test, package, review, release, and docs operations.
- Internals: implementation boundaries, lifecycles, invariants, and evidence.

The directory is the page type. Do not add visible metadata lines to tell the
reader what they are reading; make the title, introduction, and structure do
that work.

## What To Delete

Delete stale execution transcripts, issue snapshots, task copies, duplicate
planning layers, dry status pages, fake examples, and docs that cannot be
verified against current implementation evidence.

Archive only rare material with durable historical value. Archives must not be
linked as current truth.

Automation operating instructions belong in `AGENTS.md` and
`AGENTS_CONTRIBUTING.md`, not in reader-facing contributor or product docs.

## Code, Tests, Docs Move Together

Behavior, API, module-boundary, diagnostic, CLI, example, or workflow changes
must update the relevant docs and tests. Tests should verify documented intent,
not accidental current output.

## No-Claims Policy

Do not claim production readiness, public release availability, performance
superiority, package readiness, provider readiness, Node/Bun/Deno compatibility,
npm application dependency support, or complete platform support without exact
source docs and evidence.
