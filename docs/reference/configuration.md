# Configuration Reference

Sloppy uses two separate config layers:

- project source-input config (`sloppy.json`)
- app/runtime config (`appsettings*.json`, env vars, CLI overrides, and `app.config` APIs)

This page covers app/runtime config. See [sloppy.json](sloppy-json.md) for project file rules.

## Load Order

Compiler config model precedence (later overrides earlier):

1. built-in defaults
2. `appsettings.json`
3. `appsettings.<Environment>.json`
4. `appsettings.local.json`
5. `appsettings.<Environment>.local.json`
6. `.sloppy/secrets.json`
7. `.sloppy/secrets.<Environment>.json`
8. environment variables
9. Compiler CLI overrides from `sloppyc` (`--host`, `--port`, `--config`)

Default environment is `Development`.

## Built-In Defaults

| Key | Default |
| --- | --- |
| `Sloppy:Server:Host` | `127.0.0.1` |
| `Sloppy:Server:Port` | `5173` |
| `Sloppy:Server:MaxConnections` | `128` |
| `Sloppy:Server:MaxRequestBodyBytes` | `8192` |
| `Sloppy:Server:KeepAliveEnabled` | `true` |
| `Sloppy:Server:KeepAliveIdleTimeoutMs` | `5000` |
| `Sloppy:Server:MaxRequestsPerConnection` | `0` |
| `Sloppy:Server:RequestTimeoutMs` | `30000` |
| `Sloppy:Runtime:V8MicrotaskDrainLimit` | `64` |

TLS `CertificatePath` and `PrivateKeyPath` values are absolutized against config directory before Plan emission. Diagnostic messages redact TLS certificate, private-key, client-certificate, CA-bundle, and trust-store path values.

`Sloppy:Server:MaxRequestsPerConnection = 0` disables the keep-alive request count cap.

`sloppy run` also accepts these capacity keys when Plan config metadata emits
them. If they are absent, the development listener applies these runtime
defaults:

| Key | Runtime default |
| --- | --- |
| `Sloppy:Server:MaxActiveRequests` | `128` |
| `Sloppy:Server:ConnectionCapacity` | `128` |
| `Sloppy:Server:Backlog` | `128` |
| `Sloppy:Server:MaxRequestHeadBytes` | `8192` |
| `Sloppy:Server:MaxRequestWireBodyBytes` | `MaxRequestBodyBytes * 8 + 5` by default, from `SL_HTTP_TRANSPORT_DEFAULT_CHUNKED_WIRE_BODY_FACTOR` and `SL_HTTP_TRANSPORT_DEFAULT_CHUNKED_WIRE_BODY_TRAILER_BYTES` |
| `Sloppy:Server:RequestArenaBytes` | `262144`, raised to `MaxRequestBodyBytes` if needed |
| `Sloppy:Server:MaxResponseBytes` | `131072` |
| `Sloppy:Server:MaxPendingWriteBytes` | `65536` |
| `Sloppy:Server:Http2MaxStreams` | derived from `MaxActiveRequests` |
| `Sloppy:Server:DispatchOnEventLoop` | `true` |
| `Sloppy:Server:MaxDispatchesPerTick` | `32` |

Connection admission, request concurrency, and allocation capacity are separate
limits. `MaxConnections` controls accepted connection admission.
`MaxActiveRequests` controls backend request slots. `ConnectionCapacity`
controls the preallocated accepted-connection table and must be at least
`MaxConnections`. If `MaxConnections` is set and `ConnectionCapacity` is not,
`sloppy run` raises `ConnectionCapacity` to match `MaxConnections`.
`DispatchOnEventLoop` queues HTTP/1 handler dispatch into the platform loop's
dispatch phase instead of running it inline from the read/parser callback.
`MaxDispatchesPerTick` bounds how much queued HTTP/1 handler work one loop tick
drains before returning to other loop activity.

Request storage is still bounded and mostly in memory. `MaxRequestHeadBytes`
caps the HTTP/1 request head. `MaxRequestBodyBytes` caps decoded request body
bytes. `MaxRequestWireBodyBytes` caps the raw body bytes retained while waiting
for a complete request; keep it at least as large as `MaxRequestBodyBytes`.
`RequestArenaBytes` must also be at least `MaxRequestBodyBytes` because the
current request lifecycle stores the decoded body in the request arena.

When inbound TLS is enabled, the development HTTP listener can negotiate
`h2` or `http/1.1` with ALPN. There is no separate appsettings key for HTTP/2
selection; the listener chooses the protocol from ALPN,
cleartext h2c prior knowledge, h2c Upgrade, or the existing HTTP/1.1 parser.

## JSON File Requirements

- Root value must be a JSON object.
- Empty key segments are rejected.
- Nested objects flatten to colon-delimited keys.
- `${NAME}` substitutions are supported for string values; invalid substitution forms are rejected.

## Environment Variable Mapping

Accepted logical key formats include:

- `Sloppy__Server__Port`
- `SLOPPY_SLOPPY__SERVER__PORT`

Invalid forms (empty segments, triple underscores, leading underscore) are rejected with `SLOPPYC_E_CONFIG_ENV`.

Type coercion rules for env overrides:

- existing integer key -> integer parse required
- existing number key -> number parse required
- existing bool key -> `true|false`
- otherwise -> string

## CLI Overrides

Compiler overrides:

- `--host` -> `Sloppy:Server:Host`
- `--port` -> `Sloppy:Server:Port`
- repeated `--config KEY=VALUE` -> config entry (non-empty key required)

The `--config KEY=VALUE` form is a `sloppyc build` option. The `sloppy` CLI
does not accept `--config`; `sloppy build` and `sloppy run` expose source-input
metadata overrides such as `--host`, `--port`, and `--environment`.

## Runtime App Config API

App config surface (builder/provider):

- `get`, `has`, `require`
- `getString`, `getInt`, `getNumber`, `getBool`/`getBoolean`
- `getDuration`, `getSize`/`getBytes`
- `getArray`, `getObject`, `getSecret`
- `bind(prefix, schema?)`

`bind` supports descriptor types:

- `array`, `bool`, `boolean`, `bytes`, `duration`, `int`, `integer`, `number`, `object`, `secret`, `size`, `string`

`secret` descriptors cannot define literal defaults.

## Provider Configuration Keys

Provider prefix convention:

```text
Sloppy:Providers:<provider>:<name>:<field>
```

Current requirement contracts:

- sqlite: requires `database` when not supplied inline
- postgres: requires sensitive `connectionString`
- sqlserver: requires sensitive `connectionString`

## Redaction

Sensitive config keys are redacted in emitted plan values (`<redacted>`), including keys containing segments such as `password`, `secret`, `token`, `apiKey`, `privateKey`, `passphrase`, and `connectionString`.

Diagnostic redaction is stricter than current Plan redaction: TLS material path keys such as `certificatePath`, `privateKeyPath`, `clientCertificatePath`, `caBundlePath`, and `trustStorePath` are redacted from diagnostics. Inbound server certificate and private-key paths still appear in Plan config metadata today because `sloppy run` consumes those paths to configure the dev TLS listener.

## Limits

- `sloppy.json` and app config are separate systems.
- Missing live-provider env/config is a missing requirement, not a provider success.
