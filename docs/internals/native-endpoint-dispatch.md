# Native Endpoint Dispatch

Native endpoint dispatch is an internal optimization for Plan-backed web apps.
It does not change `app.get`, `app.post`, groups, modules, metadata chains, or
handler code.

## Current Contract

The compiler emits `routeDispatch` metadata in web Plans. At runtime, Sloppy
builds an arena-owned in-memory dispatch table from validated Plan routes:

- exact static paths use a method + path hash table;
- parameter routes are grouped by method and first static segment;
- route constraints are enforced by the native route pattern matcher;
- `HEAD` dispatches through `GET` and response-body suppression stays at the
  transport boundary;
- `405 Method Not Allowed` can still build an `Allow` header from route
  metadata;
- dynamic route metadata can fall back to the current generated JavaScript
  runtime path when V8 is enabled.

This is not a second public router. The source API remains the existing Sloppy
app/group/route registration API.

## Plan Metadata

`routeDispatch.mode` is currently `native-compiled-in-memory`. The Plan records
route counts, endpoint counts, exact static paths, parameter route counts,
candidate bucket counts, known constraints, and fallback counts.

`routes[].dispatch` records the endpoint ID, dispatch strategy, and execution
kind for each static Plan route. Current execution kind is `v8-handler`.

## Deferred Work

The Plan is explicit about what is not implemented yet:

- no `routes.slrt` binary artifact is emitted;
- no segment-trie node table is emitted or used;
- no native no-JS endpoint execution is advertised;
- no native URL writer table is emitted;
- catch-all route dispatch remains zero because current route syntax does not
  support catch-all segments;
- regex constraints are not part of the current supported route syntax.

Tooling must treat zero counters and `artifact.kind: "none"` as real limits,
not as hidden success.

## Inspection

Use:

```sh
sloppy routes .sloppy --dispatch
sloppy doctor .sloppy --dispatch
```

`routes` shows the dispatch table shape. `doctor` reports warnings for deferred
surfaces such as missing SLRT, segment trie, and native no-JS execution.
