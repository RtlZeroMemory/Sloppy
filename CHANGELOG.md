# Changelog

## Policy

Sloppy is pre-alpha. This changelog records source changes and dry-run artifact notes, not
public release announcements.

Entries must distinguish shipped behavior from deferred work, known limitations, and
unsupported lanes. Package or release readiness requires outside-checkout package smoke
evidence and a completed readiness gate.

## Unreleased

- Prepare public alpha docs, GitHub Pages site scaffolding, issue templates,
  and V8-required npm/package proof for the next alpha package.
- Added native JSON request/response dispatch metadata and validation work. This
  pre-alpha slice changes internal C layout for Plan and HTTP request context
  structs; native consumers must rebuild against matching headers.
- Added alpha infrastructure release dry-run and no-statements guardrail skeletons.
- Added RELEASE-DIST dry-run contracts, canonical archive naming, runtime dependency audit
  scaffolding, npm launcher/platform package skeletons, and post-merge verifier handoff.

## 0.1.0-alpha.2

- Fixes `sloppy package` for fresh packaged SQLite migrations by creating safe
  relative SQLite database parent directories from Plan metadata before package
  migrations run.
- Refreshes alpha proof fixtures for the current public templates, docs
  snippets, example classification, routes, OpenAPI output, and package
  manifests.
- Keeps the release scoped to the `@slopware` alpha package line and the
  `alpha` npm dist-tag.

## 0.1.0-alpha.1

- Carries the imported helper dependency extraction fix for function modules.
- Keeps the alpha npm package set scoped under `@slopware` and published with
  the `alpha` dist-tag.

## 0.1.0-alpha.0

- Starts the first `@slopware/sloppy` alpha package line at `0.1.0-alpha.0`.
- Stamps npm package metadata, generated archive manifests, and the runtime
  `sloppy --version` output from the same release version.
- Pins Linux release packaging to a `node:22-bullseye` glibc 2.31 build
  baseline and requires matching Linux V8 SDK metadata before package dry-run.
- Adds a manual Linux Docker published-package validation script for multiple
  glibc images, with Alpine/musl recorded as skipped until a separate musl
  package lane exists.
- Documents the Linux package as a glibc package rather than a universal Linux
  binary. This remains a public alpha, not production-ready.
