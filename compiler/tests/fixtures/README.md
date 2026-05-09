# Compiler Fixture Harness

This directory is the fixture and golden source of truth.
The checked-in fixture tree intentionally preserves the existing fixture names so current
compiler behavior and artifact goldens remain byte-stable. The harness treats each fixture
as one of these categories:

- success: fixtures with `input.js` or `input.ts` and `expected/app.plan.json`,
  `expected/app.js`, and `expected/app.js.map`;
- rejection: fixtures with `expected-diagnostics.txt`;
- diagnostics: rejected fixtures with source-located compiler diagnostics;
- golden artifacts: success fixture expected files;
- source maps: success fixtures with `expected/app.js.map`;
- compiler inference coverage: realistic supported apps, partial completeness,
  provider-kind database metadata, function-module source graph entries, route
  tags, health metadata, and invalid provider/effect shapes.

Fixtures may contain multiple source files under fixture-local subdirectories, such as
`modules/`. Optional project config such as `appsettings.json` lives beside the fixture
entrypoint when a scoped test needs it.

The provider fixtures are intentionally not SQLite-only. Generated provider execution is
covered by provider-specific V8 bridge lanes, and Plan metadata uses `capabilityKind` and `providerKind`
so PostgreSQL, SQL Server, and later non-database providers do not get forced into a
hardcoded SQLite model.
