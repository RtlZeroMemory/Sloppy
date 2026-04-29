# Config

Status: Bootstrap object config skeleton implemented.

Purpose: document the current object-backed config API and the future path to file/env
providers, redaction, validation, and diagnostics.

Implemented API example:

```ts
const builder = Sloppy.createBuilder();

builder.config.addObject({
  "server.port": 3000,
  "app.name": "hello",
});

const app = builder.build();

app.config.get("app.name");
app.config.get("missing", "fallback");
app.config.require("server.port");
app.config.has("app.name");
```

Implemented behavior:

- `builder.config.addObject(object)` accepts a plain object.
- Config keys must be non-empty strings.
- Later `addObject(...)` calls override earlier values for the same key.
- `get(key, fallback)` returns the stored value or the fallback.
- `has(key)` reports whether the key exists.
- `require(key)` returns the stored value or throws a helpful error when missing.
- Values are preserved as supplied; object values are not deep-cloned or deep-frozen.
- `builder.build()` freezes further config mutation.
- `app.config` is read-only and exposes `get`, `has`, and `require`.

Not implemented yet: JSON files, environment variables, command-line config, secret
managers, schema validation, redaction policy, diagnostics beyond thrown bootstrap errors,
native config integration, and plan emission.

Related internal docs: `docs/developer-ergonomics.md`, `docs/diagnostics.md`.
