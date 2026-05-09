# TypeScript support

`sloppyc` accepts a focused subset of TypeScript and JavaScript. The subset
is enough to write real apps but rejects anything the compiler can't extract
into the Plan.

For the full matrix, see
[reference/supported-syntax.md](../reference/supported-syntax.md). This page
is the high-level shape.

## What you can use

- ES module syntax: `import` / `export`, default and named exports.
- TypeScript type annotations on parameters, returns, and variables.
- Type aliases, interfaces, generics for typed handlers.
- Arrow functions, async functions.
- Classes, including controller classes with `static inject` arrays.
- Const and let, template literals, destructuring, spread.
- Object and array literals as handler return values.
- Tagged template literals for `sql\`...\``.
- Plain conditionals, ternaries, switch.

## What you can't use yet

- Importing arbitrary npm packages. Imports must be `"sloppy"`,
  `"sloppy/<subpath>"`, or relative paths.
- Dynamic `import()`.
- `node:` prefix imports.
- Top-level `await`.
- Decorators (the controller surface uses `static inject` instead).
- Conditional or loop-based route registration.
- Computed method names on `app.<verb>(...)` calls.
- `eval`, `Function` constructor.
- Mutable module state read by the compiler (statics or simple constants
  are fine; a route table built by a `for` loop is not).

If your code uses something the compiler doesn't understand yet, you'll
get a diagnostic that points at the source location and suggests an
alternative. Diagnostics never silently strip code.

## Imports

```ts
// Public surface
import { Sloppy, Results, sql, schema } from "sloppy";
import { data } from "sloppy/data";   // alternate subpath

// Relative
import { usersModule } from "./users";
```

Subpath imports under `"sloppy/..."` are reserved for the runtime stdlib —
see [API](../api/README.md) for what's exported from each.

## Async handlers

```ts
app.get("/users/{id:int}", async (ctx) => {
    const user = await loadUser(ctx.route.id);
    return user ? Results.ok(user) : Results.notFound();
});
```

The runtime awaits the returned promise during the owner-thread microtask
drain. Long-running awaits aren't supported; if your handler depends on
multi-second background work, queue it via `WorkQueue` and return
`Results.accepted({ jobId })`.

## Type-driven handler bindings

Framework v2 typed handlers let you declare what a handler needs in the
parameter list:

```ts
import { Sloppy, Results, sql } from "sloppy";
import { Route, Query, Body } from "sloppy";
import { Sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();

app.get("/users/{id:int}", (
    id:    Route<number>,
    db:    Sqlite<"main">,
) =>
    db.queryOne(sql`SELECT id, name FROM users WHERE id = ${id}`)
);

app.post("/users", (
    body:  Body<{ name: string; email: string }>,
    db:    Sqlite<"main">,
) =>
    db.exec(sql`INSERT INTO users (name, email) VALUES (${body.name}, ${body.email})`)
);
```

The compiler infers route bindings, body schemas, provider injections, and
service capabilities from these types.

> Experimental. Typed handlers cover the common path end-to-end for
> SQLite, route/query/header/body bindings, and `Service<T>` injection.
> Edge cases may still emit a less-specific generated wrapper. If a typed
> handler doesn't compile, fall back to the explicit `(ctx) => …` style.

## Common gotchas

- **Import paths must be statically analyzable.** `import("./" + name)` is
  rejected.
- **Routes register at module load.** Don't wrap registrations in
  `if (process.env.NODE_ENV …)` — there is no `process.env` in Sloppy
  source. Use config-driven decisions inside the handler instead.
- **Top-level side effects should be cheap.** The compiler evaluates simple
  literal expressions; complex top-level work is rejected because it makes
  the Plan non-deterministic.
