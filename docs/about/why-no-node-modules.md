# Why no `node_modules`?

Sloppy apps don't import packages from `node_modules`. The compiler
resolves only `"sloppy"`, `"sloppy/<subpath>"`, and relative paths.

This is a design decision, not an oversight, and it follows directly from
the [compiler-first runtime](compiler-first-runtime.md) model.

## The reasoning

The compiler needs to read every module in the app graph and reason about
it. That means the import surface has to be analyzable. Sloppy supports a
focused subset of TypeScript and bans things like dynamic imports because
they make the graph non-deterministic.

Loading arbitrary npm dependencies opens the same problem space at a much
larger scale:

- Most npm packages assume Node or browser globals (`process`, `Buffer`,
  `fs`, `setImmediate`, …) that Sloppy doesn't provide.
- Many use dynamic `require`/`import` to choose backends, which the
  compiler can't follow.
- Some bundle native addons that wouldn't load in Sloppy's V8 isolate.
- Versioning, peer dependencies, and resolution semantics are themselves
  a design space.

Quietly running every npm package would mean Sloppy is "Node, but worse"
for those packages. We'd rather not pretend.

## What to do instead

For the cases where you'd reach for an npm package today:

| You'd use…              | In Sloppy do…                                     |
| ----------------------- | ------------------------------------------------- |
| `express`, `fastify`    | The first-party app/router/results surface        |
| `pg`, `mysql2`, `mssql` | `data.postgres`, `data.sqlserver`                 |
| `better-sqlite3`        | `data.sqlite`                                     |
| `zod`, `yup`            | `schema` for simple cases; vendor a small library if needed |
| `dotenv`, `config`      | `appsettings.json` + the config sources           |
| `winston`, `pino`       | The first-party `Logger`                          |
| `bcrypt`, `argon2`      | `Password` from `sloppy/crypto`                   |
| `ulid`, `uuid`          | `crypto.randomUUID()` in V8                       |
| `node-fetch`, `axios`   | `HttpClient` from `sloppy/net`                    |

For things genuinely outside Sloppy's stdlib (a specific message broker
SDK, a vendor-specific protocol), the practical options are:

- Vendor a small dependency-free implementation into your repo.
- Run the third-party logic in a separate process and call it over HTTP
  from Sloppy.
- Wait for first-party support, or contribute it.

## Will this change?

Maybe, eventually. Adding npm import support means designing how the
compiler handles the resolution algorithm, how Plan determinism survives
lockfile churn, how diagnostics work for opaque dependencies, and how
audit/security checks extend across the boundary. None of that is shallow
work, and pre-alpha effort is going into the parts of Sloppy that are
load-bearing today.

If your project depends on a specific npm package, treat it as a vote for
that area to be designed when the runtime is more stable.
