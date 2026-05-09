# Documentation Policy

Documentation is part of how Sloppy ships. A change is incomplete if the
next reader is sent to stale behavior or unsupported claims.

## Source of truth

Current docs are written from current source code, public headers,
compiler code, stdlib code, CLI code, scripts, tests, examples, and
commands actually run. Old docs are discovery material. GitHub issues
own the live roadmap and task state.

## Where things live

`docs/` is organized for the reader, not for the author:

- `docs/install.md`, `docs/quickstart.md` — start here
- `docs/api/` — first-party TypeScript API
- `docs/cli/` — `sloppy` and `sloppyc` commands
- `docs/guide/` — task-shaped walkthroughs and project conventions
- `docs/reference/` — exhaustive lookup material
- `docs/about/` — design notes, background, motivation
- `docs/internals/` — implementation-level docs for contributors
- `docs/contributor/` — building, testing, releasing
- `docs/release/` — release artifact policy
- `docs/glossary.md` — vocabulary

A page is the type of its directory. The title and structure should make
that obvious; you don't need a metadata header announcing it.

## What to delete

Delete stale execution transcripts, issue snapshots, task copies, dry
status pages, fake examples, and docs that don't match the current
implementation. Archive only material with durable historical value.

Automation operating instructions live at the repository root, outside the
reader-facing product and contributor docs.

## Code, tests, docs move together

Behavior, API, module-boundary, diagnostic, CLI, example, or workflow
changes update the relevant docs and tests in the same change. Tests
verify documented intent, not accidental current output.

## Tone

Write for a developer who wants to use Sloppy. Direct sentences, real
examples, code that runs. Mark experimental and planned surfaces
explicitly. Don't paragraph-hedge — short, accurate sentences beat long
defensive ones.
