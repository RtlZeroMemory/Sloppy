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
- Added alpha infrastructure release dry-run and no-statements guardrail skeletons.
- Added RELEASE-DIST dry-run contracts, canonical archive naming, runtime dependency audit
  scaffolding, npm launcher/platform package skeletons, and post-merge verifier handoff.

## 0.1.0-alpha.0

- Early alpha npm package records were published, but they did not prove
  V8-backed handler execution. Treat this version as incomplete for the public
  runtime install path.
