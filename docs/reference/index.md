# Reference

Lookup material. Conceptual guides live under [guide/](../guide/index.md);
this section is for "what does this flag/field/format actually do".

## Format and configuration

- [`sloppy.json`](sloppy-json.md) — project descriptor schema
- [Configuration](configuration.md) — config sources, key model, typed binding
- [Plan format](plan-format.md) — full `app.plan.json` schema
- [Dependency graph](dependency-graph.md) — package/module graph metadata
- [Node compatibility](node-compatibility.md) — builtin registry and shim status

## API reference

These pages duplicate content from [api/](../api/index.md) at a more
exhaustive level — they're there for "I need every option" lookup.

- [Routing](routing.md)
- [Request context](request-context.md)
- [Results](results.md)
- [Dependency injection](dependency-injection.md)
- [Validation](validation.md)
- [Workers](workers.md)
- [Data API](data-api.md)
- [Providers](providers.md)
- [Diagnostics](diagnostics.md)

## Compiler

- [Supported syntax](supported-syntax.md) — exact compiler subset and error codes
- [Framework metadata](framework.md) — what the Plan carries from typed handlers

## Project

- [Stability](stability.md) — schema versions, what's pinned and what isn't
- [Platform status](platform-status.md) — which platforms are supported, to what level
- [Dependencies](dependencies.md) — V8, libpq, ODBC, OpenSSL — when they matter
