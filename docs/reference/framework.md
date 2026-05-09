# Framework Reference

This page documents the current JavaScript framework surface from `stdlib/sloppy/app.js` and bootstrap tests.

## Imports

Root runtime exports come from `sloppy` (for example `Sloppy`, `Router`, `Results`, `schema`, `data`, `sql`).

Provider descriptor registration currently has one runtime module:

```ts
import { sqlite } from "sloppy/providers/sqlite";
```

Compiler metadata markers such as `Route<T>`, `Query<T>`, `Body<T>`, `Header<...>`, `RequestContext`, `Service<T>`, `Config<...>`, `Sqlite<...>`, `Postgres<...>`, and `SqlServer<...>` are compile-time extraction shapes, not Node-compatibility claims.

## Sloppy Object

| API | Behavior |
| --- | --- |
| `Sloppy.create()` | Returns a built app with default builder state. |
| `Sloppy.createBuilder()` | Returns a mutable builder (`config`, `logging`, `capabilities`, `services`, `addModule`, `build`). |
| `Sloppy.module(name)` | Creates a module descriptor with capability/service/route phases. |

## App Object

| API | Behavior |
| --- | --- |
| `config`, `log`, `services`, `capabilities` | Built providers. |
| `use(providerOrWorker)` | Accepts worker resources and Sloppy provider descriptors. Current provider kind accepted by app validation: `sqlite`. |
| `useModule(moduleOrFactory)` | Accepts route-only `Sloppy.module(...)` or named synchronous function modules. |
| `mapGet/mapPost/mapPut/mapPatch/mapDelete` | Route registration methods. |
| `get/post/put/patch/delete` | Aliases for `map*`. |
| `mapGroup` / `group` | Route grouping helpers. |
| `mapController` / `controller` | Controller mapper APIs. |
| `freeze()` / `isFrozen()` | Freeze app mutation state. |
| `__getRoutes()`, `__debug()`, `__getModuleGraph()`, `__getPlanContributions()` | Tested introspection helpers used by tooling/tests. |

## Builder Behavior

- `build()` runs module phases in dependency order.
- Capability phases run first, then service phases, then route phases.
- `build()` freezes builder mutation.
- Duplicate module names are rejected.
- Missing module dependencies and dependency cycles are rejected.
- Async module phase callbacks are rejected.

## Module Rules

- Module names must be lowercase.
- `dependsOn(...)` creates dependency edges.
- Duplicate module registration is rejected.
- Route-only module descriptors can be used directly with `app.useModule(...)`.
- Function modules must be named and synchronous.

## Provider Descriptors In Framework Registration

`sqlite(name, options?)` returns a frozen descriptor with:

- `__sloppyProvider: true`
- `kind: "sqlite"`
- `name`
- `token`: `data.<name>` unless `name` already contains a dot
- `options`

Provider descriptor name validation:

- non-empty string
- no leading/trailing whitespace
- pattern: letters, digits, dot, underscore, hyphen

Merged provider options at `app.use(...)` must satisfy current sqlite checks, including non-empty `database`.

## Mutation And Lifecycle Errors

Common enforced errors:

- duplicate route registration (`method + pattern`)
- duplicate service registration
- app/builder frozen mutation
- circular service dependencies
- singleton resolving scoped dependency
- root service resolution for non-singleton services

## Limits

- `app.use(...)` provider validation is currently sqlite-only.
- Double-underscore methods are usable and tested, but remain internal-oriented surfaces.
- Handler execution through `sloppy run` remains V8-gated.
