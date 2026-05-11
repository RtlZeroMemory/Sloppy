# The Plan model

Every Sloppy build produces an `app.plan.json` file. The Plan is the
contract between the compiler and the runtime — a deterministic JSON
description of what your app needs, what routes it exposes, and what
capabilities it uses.

If you've used Node, Bun, or Deno, you've never had a Plan. They discover
the app while running it. Sloppy figures out the shape first, then runs
the app against that shape.

## What's in a Plan

```jsonc
{
  "schemaVersion": "plan/v1-alpha",
  "compiler":      { "version": "0.x.y" },
  "runtime":       { "minimumVersion": "0.x.y" },
  "artifacts":     [
    { "name": "app.js",     "hash": "sha256:…" },
    { "name": "app.js.map", "hash": "sha256:…" }
  ],
  "routes":        [
    { "method": "GET", "pattern": "/users/{id:int}", "handlerId": 1, "name": "Users.Get" }
  ],
  "handlers":      [ { "id": 1, "kind": "registered" } ],
  "capabilities":  [
    { "token": "data.main", "kind": "database", "provider": "sqlite", "access": "readwrite" }
  ],
  "providers":     [ … ],
  "requiredFeatures": [ "stdlib", "http", "sqlite", … ],
  "config":        { … },
  "server":        { "host": "127.0.0.1", "port": 5173, … }
}
```

The exact schema is documented in
[reference/plan-format.md](../reference/plan-format.md). The shape above
covers the load-bearing fields.

## What the runtime does with it

When `sloppy run` starts, before evaluating any JavaScript:

1. Reads `app.plan.json`.
2. Validates the schema version against the runtime's supported versions.
3. Hashes the listed artifact files and compares against the recorded hashes.
4. Checks every entry in `requiredFeatures` against the runtime feature
   registry. Missing features fail the load.
5. Builds the route table from `routes`.
6. Activates declared providers and capabilities.
7. Only then evaluates `app.js` inside V8.

If any of those steps fail, the runtime exits with a structured diagnostic
and never executes the bundle.

## Why have a Plan

A few reasons that compound:

- **You can introspect the app without running it.** `sloppy routes`,
  `sloppy capabilities`, `sloppy openapi`, and `sloppy audit` all work off
  the Plan. None of them spin up V8.
- **The runtime can fail fast on missing dependencies.** If your app needs
  PostgreSQL and the PostgreSQL provider dependency is unavailable, or if it
  needs SQL Server and Microsoft ODBC Driver 17 or 18 is unavailable, Sloppy
  reports that provider-specific problem instead of half-booting and crashing
  on the first query. SQLite apps are unaffected.
- **Tooling has a stable target.** OpenAPI generation, security audits, and
  codegen consumers can read the Plan instead of grepping source.
- **Determinism.** A given source + compiler version produces the same
  Plan, byte-for-byte.

## The Plan is not a manifest

Things the Plan deliberately is *not*:

- It isn't an npm `package.json`. Package-manager metadata stays in
  `package.json`; Sloppy records compatible bundled modules in dependency graph
  metadata instead of using the Plan as a package manager lockfile.
- It isn't a deployment descriptor. It says nothing about how to run the
  app on Kubernetes or anywhere else.
- It isn't user-editable. Editing it by hand is supported in the sense that
  nothing stops you, but the next `sloppy build` will overwrite it.

## Hand-editing edge cases

The only time you'd open `app.plan.json` is to:

- Read it for debugging.
- Hash it for cache keys.
- Diff two builds to see what changed.

If you need different Plan output, the right move is to change the source
or compiler flags, not to edit the artifact.

## Plan version

The schema version is recorded as `schemaVersion: "plan/v1-alpha"`.
Public alpha, pre-production breaking changes are possible. If a runtime
upgrade rejects an old Plan, rebuild with the matching compiler.
