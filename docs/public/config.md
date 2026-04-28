# Config

Status: Planned / not implemented yet.

Purpose: document future configuration sources, required keys, redaction, and diagnostics.

Planned API example:

```ts
builder.config
  .addJsonFile("sloppy.json", { optional: true })
  .addEnv("APP_");
```

This example is aspirational and not currently implemented.

Related internal docs: `docs/developer-ergonomics.md`, `docs/diagnostics.md`.
