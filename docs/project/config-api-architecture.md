# CORE-CONFIG-01 Configuration Architecture

Configuration is part of the Sloppy app Plan. It is not an environment-variable shortcut
and it is not Node compatibility.

## Sources

Source precedence is deterministic. Later sources override earlier sources:

1. built-in runtime defaults;
2. `appsettings.json`;
3. `appsettings.{Environment}.json`;
4. `appsettings.local.json`;
5. `appsettings.{Environment}.local.json`;
6. `.sloppy/secrets.json` and `.sloppy/secrets.{Environment}.json` when present;
7. environment variables with `__` hierarchy mapping;
8. `sloppyc build --config Key=Value`;
9. explicit bootstrap/test `builder.config.addObject(...)` overlays.

Missing optional files are allowed. Malformed JSON, invalid key segments, invalid
environment hierarchy, invalid typed conversion, and missing provider-owned configuration
produce stable diagnostics. Runtime artifact/config file reads are internal host reads and
remain separate from public `sloppy/fs` policy.

## Keys

Canonical keys use `:`:

```text
Sloppy:Providers:postgres:main:connectionString
Auth:JwtSecret
```

Environment variables use `__`:

```text
Sloppy__Providers__postgres__main__connectionString
Auth__JwtSecret
```

The legacy `SLOPPY_` prefix remains accepted for Sloppy-owned defaults.

## Typed Access

The bootstrap app-host config API supports:

- `getString`, `getInt`, `getNumber`, `getBool`, and `getBoolean`;
- `getDuration`, returning milliseconds from numbers or strings such as `2s` and `15m`;
- `getSize` and `getBytes`, returning bytes from numbers or strings such as `4MiB`;
- `getArray`, `getObject`, and `getSecret`;
- `bind(prefix, descriptor)` for typed object binding.

Secret values return redacted wrappers from `getSecret` and `bind(..., "secret")`.
Stringification, JSON serialization, doctor output, Plan metadata, tests, and examples must
not print the raw value.

## Plan Metadata

`sloppyc` emits:

- `configuration.environment`;
- `configuration.keys[]` with winning source and redacted values;
- `configuration.providers[]` for provider config bindings;
- `configuration.requirements[]` for static reads, static bind descriptors, and
  provider-owned config contracts;
- `configuration.packageManifest.required[]` and `.optional[]`;
- `configReads[]` for source-level static reads.

Dynamic keys are represented honestly. Static keys are emitted; dynamic keys are rejected
in compiler-owned static contexts instead of being guessed.

## Deferred

Reload-on-change, production vault integrations, custom/remote providers, and provider
runtime bridges beyond current executable provider support are deferred. They must not be
reported as implemented behavior by CORE-CONFIG-01.
