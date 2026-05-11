# Config

Configuration is structured key/value data with typed accessors. Register
sources on the builder, then read them from services, modules, and handlers.

```ts
const builder = Sloppy.createBuilder();

builder.config.addObject({
    "app:greeting": "hello",
    "server:port": 5173,
});

const app = builder.build();

app.get("/", (ctx) =>
    Results.text(ctx.config.getString("app:greeting"))
);
```

## Sources

The JS builder exposes one source today:

```ts
builder.config.addObject({ "app:name": "demo" });
```

Other sources — `appsettings.json`, `appsettings.{Environment}.json`,
environment variables, command-line — are read by the compiler/Plan
pipeline before the JS code runs. They land in the same config provider
your handlers see, but you don't add them from JS code; they're picked
up automatically from the project layout. See
[guide/project-layout.md](../guide/project-layout.md).

## Keys and environment variables

Configuration keys are case-insensitive and use `:` as a path
separator. Internally they're normalized to uppercase:

```ts
builder.config.addObject({ "app:Name": "demo" });

ctx.config.getString("APP:NAME");      // "demo"
ctx.config.getString("app:name");      // "demo"
```

Environment variables are a different surface. Because most shells
forbid `:` in variable names, **environment variables use double
underscore (`__`) as the separator**, and the runtime maps them onto
the `:` form when reading config:

```
APP__GREETING=hello                 → key "app:greeting"
SLOPPY__PROVIDERS__POSTGRES__MAIN__CONNECTIONSTRING=...
                                    → key "sloppy:providers:postgres:main:connectionstring"
```

Provider keys follow a stable shape:

```
SQLite    "sloppy:providers:sqlite:<name>:*"
Postgres  "sloppy:providers:postgres:<name>:*"
SQLServer "sloppy:providers:sqlserver:<name>:*"
```

You usually don't write these keys by hand — provider examples and
modules read environment variables explicitly via
`Environment.get(...)` from `sloppy/os` and pass the value to
`data.<provider>.open(...)`.

## Reading values

The public `Config` helper also exposes reference descriptors for APIs that
resolve configuration later:

| Helper | Behavior |
| --- | --- |
| `Config.required(key)` | Required secret-safe config reference used by auth and metadata extraction. |
| `Config.boolean(key, fallback?)` | Boolean config reference for policy options such as `app.useErrors({ includeDetails })`. |

`ctx.config` and `app.config` both expose:

| Method                                | Returns                            |
| ------------------------------------- | ---------------------------------- |
| `get(key, fallback?)`                 | Raw value or `fallback`            |
| `has(key)`                            | `boolean`                          |
| `require(key)`                        | Raw value, throws if missing       |
| `getString(key, fallback?)`           | `string`                           |
| `getInt(key, fallback?)`              | `number` (integer)                 |
| `getNumber(key, fallback?)`           | `number`                           |
| `getBool(key, fallback?)`             | `boolean`                          |
| `getDuration(key, fallback?)`         | `number` (milliseconds)            |
| `getSize(key, fallback?)`             | `number` (bytes)                   |
| `getBytes(key, fallback?)`            | `number` (bytes; alias)            |
| `getArray(key, fallback?)`            | array                              |
| `getObject(key, fallback?)`           | plain object                       |
| `getSecret(key)`                      | `ConfigSecretValue` (see below)    |
| `bind(prefix, schema?)`               | typed snapshot bound under prefix  |

The typed getters parse strings into the expected type, so `addObject` can
mix raw types with strings:

```ts
builder.config.addObject({
    "server:port": "5173",        // parsed as int
    "server:request-limit": "1mb",// parsed as 1_048_576
    "server:idle-timeout": "30s", // parsed as 30_000 ms
});
```

### Duration units

`getDuration` parses suffixes: `ms`, `s`, `m`, `h`. A bare number is
milliseconds.

```
"500ms" → 500
"30s"   → 30000
"5m"    → 300000
"2h"    → 7200000
```

### Size units

`getSize` / `getBytes` parses both decimal (`kb`, `mb`, `gb` = 1000⁻based)
and binary (`kib`, `mib`, `gib` = 1024-based) suffixes.

```
"512b"  → 512
"1kb"   → 1000
"1kib"  → 1024
"1mb"   → 1000000
```

## Secrets

Sensitive values are wrapped:

```ts
const secret = ctx.config.getSecret("db:password");
const value = secret.value();
```

The wrapper exists so secrets don't accidentally end up in logs,
snapshots, or diagnostics. `secret.toString()` returns
`"[Secret redacted]"`. The real value is only available through
`secret.value()` — handler code can still leak it if it unwraps and
logs explicitly.

## Binding

`config.bind(prefix, schema?)` returns a frozen object with typed fields
under `prefix`:

```ts
const server = ctx.config.bind("server", {
    port:        { type: "int", default: 5173 },
    host:        { type: "string", default: "127.0.0.1" },
    requestLimit:{ type: "size", default: "1mb" },
});

server.port;          // number
server.requestLimit;  // bytes
```

Schemas accept descriptors with:

| Field         | Notes                                              |
| ------------- | -------------------------------------------------- |
| `type`        | `"string"`, `"int"`/`"integer"`, `"number"`, `"bool"`/`"boolean"`, `"duration"`, `"size"`/`"bytes"`, `"array"`, `"object"`, `"secret"` |
| `default`     | value used when the key is missing                 |
| `required`    | `true` to throw when the key is missing            |
| `enum`/`values` | allowed values                                    |
| `min` / `max` | numeric bounds                                     |
| `secret`      | mark string fields as secret                       |

Without a schema, `bind` returns a plain object containing every key under
the prefix as a raw value.
