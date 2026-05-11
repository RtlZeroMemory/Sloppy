# Compiler-first runtime

Most JavaScript runtimes — Node, Bun, Deno — are *discovery-first*. You
hand them a script. They evaluate it, and the application's shape becomes
visible only as code runs: routes register lazily, dependencies load on
first import, configuration is read by whatever happens to need it.

That's flexible, and it's also a lot of mystery at runtime.

Sloppy goes the other way. The compiler reads supported source up front
and writes down everything it can prove:

```text
your code        ──►  sloppyc  ──►  app.plan.json
                                    app.js
                                    app.js.map
```

Then the runtime reads the Plan first, validates it, and refuses to start
if anything looks wrong. Only after that does it evaluate `app.js`.

## What that buys you

**Tooling without execution.** `sloppy routes`, `sloppy capabilities`,
`sloppy openapi`, `sloppy audit` all work off `app.plan.json`. None of
them touch V8. You can lint your route table, generate API docs, and run
a security audit on a host that doesn't even have V8 installed.

**Fail-fast startup.** If your app needs a PostgreSQL provider and PostgreSQL
client support is unavailable, or if it needs SQL Server and Microsoft ODBC
Driver 17 or 18 is unavailable, the runtime says so and exits instead of
half-booting and crashing on the first query. SQLite apps do not need those
optional provider dependencies.

**Determinism.** A given source + compiler version produces the same
Plan, byte-for-byte. Plans are fingerprintable cache keys.

**A clean target for codegen.** The Plan is JSON with a stable schema. Any
tool can read it. Add a custom OpenAPI generator, a service-graph
visualizer, an IDE extension — none of them have to parse TypeScript.

## What that costs

**A bounded source subset.** The compiler can extract structure only from
code it can statically analyze. You can't `for`-loop over an array of
route objects and call `app.get(...)` in the loop body. You can't
`import("./" + name)`. The constraints come with diagnostics that point
at the source location.

**Edit-build-run instead of edit-run.** Source changes go through
`sloppyc` before the runtime sees them. `sloppy run src/main.ts` does this
in one step, but there's still a compile pass.

**Less ecosystem reuse.** Sloppy can bundle compatible installed JavaScript
packages, but it does not run directly from `node_modules` at runtime. The
first-party stdlib aims to cover what a backend usually needs; everything
beyond that must fit the sealed artifact graph or run outside Sloppy.

## What it isn't

- **Not AOT compilation in the V8 sense.** The bundle is JavaScript that
  runs on V8 like any other. The Plan is metadata, not bytecode.
- **Not a static typechecker for arbitrary TS.** `sloppyc` parses
  TypeScript syntax and uses simple inference for handler bindings, but
  it doesn't replace `tsc`. Use `tsc` in your editor for full type
  checking.

The trade is opinionated and deliberate. Sloppy is for backends where
shape-up-front is worth more than maximum flexibility.
