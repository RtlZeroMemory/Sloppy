# Health checks

`app.mapHealthChecks(options?)` installs three GET routes — aggregate,
liveness, and readiness — that report the app's health.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapHealthChecks({
    checks: [
        {
            name: "database",
            check: async (ctx) => Boolean(await ctx.services.get("db").ping()),
            readiness: true,
        },
        {
            name: "shard-leader",
            check: () => Boolean(globalThis.__shardLeader),
            liveness: true,
            readiness: false,
        },
    ],
});
```

Three routes get installed in one shot:

| Path                                | Mode       | Runs                                          |
| ----------------------------------- | ---------- | --------------------------------------------- |
| `/health`                           | aggregate  | every check, regardless of liveness/readiness |
| `/health/live`                      | liveness   | only checks with `liveness: true`             |
| `/health/ready`                     | readiness  | only checks with `readiness: true` (default)  |

If any of the three paths is already registered, `mapHealthChecks` throws
before installing any route — partial registration cannot leave the app in
a half-configured state.

## Default paths

```ts
app.mapHealthChecks();              // /health, /health/live, /health/ready
app.mapHealthChecks("/_status");    // override aggregate path only
app.mapHealthChecks({ path: "/_health", livenessPath: "/_alive", readinessPath: "/_ready" });
```

The three paths must be distinct. Without overrides, the defaults are
`/health`, `/health/live`, `/health/ready`.

## Check definitions

A check is either a function or a `{ name, check, liveness?, readiness? }`
object.

```ts
function database() { /* ... */ }
function shardLeader() { /* ... */ }

app.mapHealthChecks({
    checks: [
        database,                                         // name = "database", readiness only
        { name: "leader", check: shardLeader, liveness: true, readiness: false },
    ],
});
```

- A bare function: name is `function.name` (or `check-N` for anonymous), runs
  during readiness and aggregate.
- An object: `name` and `check` are required; `liveness` and `readiness` are
  booleans (defaults: `liveness: false`, `readiness: true`). At least one
  must be true.

A check function can return:

- `true` / `false` — explicit health.
- `{ ok: boolean }` — explicit health.
- `undefined` or anything else — treated as healthy.

A thrown or rejected check is unhealthy.

## Response shape

```json
{
  "status": "healthy",
  "checks": [
    { "name": "database", "status": "healthy" }
  ]
}
```

`200 application/json` when every check passed. `503 application/json` when
any check returned `false`, `{ ok: false }`, or threw — body shape is the
same.

## Status

Health-check routes run in the bootstrap app-host handler path. `sloppyc`
also extracts literal `app.mapHealthChecks(...)` calls for emitted artifacts:
it generates aggregate, liveness, and readiness GET handlers and emits
Plan-level health metadata with endpoint kind and selected check names.
