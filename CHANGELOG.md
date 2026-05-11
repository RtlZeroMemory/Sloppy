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

## v0.1.0-alpha.3 - Public alpha refresh

- Web Mode framework improvements: request/response helpers, routing metadata,
  static assets, forms, cookies, multipart uploads, serialization/content
  negotiation, ProblemDetails error output, OpenAPI output, and route
  precedence/URL generation are included in the alpha proof surface.
- Program Mode, CLI templates, package-api dependency graph support, dev watch,
  Node-compatible pure-JS package expansion, validation/schema enforcement,
  JWT/API key/cookie-session auth helpers, database migrations, and `sloppy db`
  are included in the package validation lane.
- The release includes FFI foundation work, native validation enforcement,
  alpha proof goldens, fuzz targets, docs-snippet checks, and public docs
  consolidation.
- This refresh rebuilds from current `origin/main` after the compiler metadata
  relaxation and neutral local runtime benchmark harness landed. Benchmarks
  remain engineering/regression evidence, not superiority claims.
- Packaged apps now create package-local parent directories for relative SQLite
  database paths and resolve package-run SQLite paths from the package root, so
  the documented package migration/run flow works outside the source checkout
  without copying live database contents.
- Windows x64 and Linux x64 glibc npm platform packages are the supported
  package targets for this alpha. macOS npm platform packages are deferred
  until Mac-built artifacts and registry install smoke evidence exist.
- PostgreSQL provider packages are not bundled in this npm alpha. PostgreSQL
  and SQL Server live-provider tests require external services; SQL Server
  ODBC remains system- or organization-managed.
- Realtime route metadata and current SSE/WebSocket status are documented, but
  native WebSocket upgrade support is not claimed unless the runtime lane
  reports it as available.
- This is a public alpha, pre-production release. It is not production-ready,
  not full Node compatibility, does not support native addons/N-API, and does
  not make benchmark superiority claims.

## 0.1.0-alpha.0

- Early alpha npm package records were published, but they did not prove
  V8-backed handler execution. Treat this version as incomplete for the public
  runtime install path.
