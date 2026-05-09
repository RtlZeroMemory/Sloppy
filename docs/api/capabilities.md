# Capabilities

Capabilities are declarations of *what your app needs to do*. Today they
cover databases — declaring a database capability tells the runtime which
provider to wire up and how the app intends to use it.

```ts
const builder = Sloppy.createBuilder();

builder.capabilities.addDatabase("data.main", {
    provider: "sqlite",
    access: "readwrite",
});

const app = builder.build();
```

The capability `"data.main"` is then available to services and handlers via
`app.capabilities` / `ctx.capabilities`.

## Declaring a database capability

```ts
builder.capabilities.addDatabase(token, {
    provider: "sqlite" | "postgres" | "sqlserver",
    access:   "read" | "write" | "readwrite",
});
```

| Field      | Notes                                              |
| ---------- | -------------------------------------------------- |
| `provider` | Provider implementation backing the capability     |
| `access`   | Maximum access the capability grants               |

The `token` is a free-form string. Convention is dotted lowercase
(`"data.main"`, `"data.replicas.eu"`). Tokens are how `data.*` calls find
the capability:

```ts
const main = await ctx.services.get("data.main");
// or, for compiler-inferred providers:
//   data.sqlite.open({ capability: "data.main", database: ":memory:" });
```

## Reading capabilities

```ts
const cap = app.capabilities.get("data.main");
// {
//   token:     "data.main",
//   kind:      "database",
//   provider:  "sqlite",
//   access:    "readwrite",
//   metadata:  {},
//   module:    null,
// }

app.capabilities.list();
// [...all declared capabilities]
```

## Module capabilities

A module can declare its own capabilities. They merge into the app's
capability set with the module name attached:

```ts
const Users = Sloppy.module("users")
    .capabilities((caps) => {
        caps.addDatabase("data.users", {
            provider: "sqlite",
            access: "readwrite",
        });
    });

app.useModule(Users);
app.capabilities.get("data.users").module === "users";
```

## What capabilities are not

- **Not an OS sandbox.** The runtime checks capabilities before opening
  resources, but it doesn't constrain the rest of the process.
- **Not an authorization system.** Capabilities describe what the *app*
  declares about itself, not what end users can do.

## What's planned

Filesystem and network capabilities are planned (`addFs`, `addNetwork`).
Today, only `addDatabase` is wired up.
