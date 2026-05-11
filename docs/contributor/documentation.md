# Documentation

Docs are part of every behavior change. The repo-wide policy lives at
[docs/documentation-policy.md](../documentation-policy.md); this page
is the day-to-day shape.

## Where things go

| You wrote…                                     | It belongs in…                          |
| ---------------------------------------------- | --------------------------------------- |
| A new public API or changed argument shape     | `docs/api/<area>.md`                    |
| A new CLI command or flag                      | `docs/cli/<command>.md`                 |
| A walkthrough or task-shaped guide             | `docs/guide/<topic>.md`                 |
| A schema, error code, or matrix                | `docs/reference/<topic>.md`             |
| A design note / "why we did it this way"        | `docs/about/<topic>.md`                 |
| A new boundary, lifecycle, or invariant         | `docs/internals/<area>.md`              |
| A contributor workflow change                   | `docs/contributor/<topic>.md`           |
| Release artifact policy                        | `docs/release/`                         |

A behavior change without a matching doc update is a missing-docs
finding at review.

## Tone

- Direct, specific, code-first.
- One thing per page; index pages link to siblings.
- Mark experimental and planned surfaces explicitly. Don't paragraph-
  hedge.
- Examples are runnable. `sloppy build` and `sloppy run` mean what
  they mean — don't paste pseudo-commands.
- Avoid `Status: draft` headers and other meta-prefaces.
  The directory and the title carry that information.

## What not to do

- Don't paste planning transcripts or workflow notes
  into docs.
- Don't add `Type:`/`Status:` metadata lines at the top of pages.
- Don't keep stale planning notes as if they were current docs —
  delete or move to an archive.
- Don't make claims that aren't backed by current code or tests.

## Cross-links

Use relative paths (`../api/data.md`, not `docs/api/data.md`). The
docs hygiene scanner catches broken cross-links in `lint`.

## Updating goldens that contain doc-flavored text

Some goldens (CLI help, OpenAPI, doctor output) double as user-visible
text. Updating them is a doc change in spirit — explain the intent in
the PR body the same way you would for any doc.

## When you intentionally change *only* docs

Fine. Run `dev.ps1 lint` and `git diff --check`. Note in the PR body
that no tests were added (and why), so reviewers don't assume an
omission.
