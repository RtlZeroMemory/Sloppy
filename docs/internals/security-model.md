# Security model

Sloppy's security posture is **auditable boundaries**, not an OS
sandbox. The runtime decides what an app can do based on declared
capabilities, validates that decision at startup, and redacts secrets
out of diagnostics.

The user-facing model lives in [about/security.md](../about/security.md).
This page documents how it's enforced.

## Boundaries

| Boundary                    | Enforced where                              | Failure shape                |
| --------------------------- | ------------------------------------------- | ---------------------------- |
| Secret in Plan field        | `src/core/plan_parse.c` redaction sweep      | startup rejection            |
| Provider/capability mismatch| `src/core/plan_parse.c`, `src/core/app_host.c` | startup rejection         |
| Missing runtime feature     | `src/core/features.c`                        | startup rejection            |
| Connection-string in error  | `src/data/<provider>.c` redaction helpers    | redacted diagnostic          |
| Raw native pointer in JS    | `src/engine/v8/*`, resource table            | bridge rejection             |
| OS-level confinement        | not enforced (documented limitation)         | —                            |

Each row's enforcement is in code, not policy. A PR that, say, prints a
connection string into a diagnostic violates the redaction tests.

## Capability validation

Two passes:

1. **Plan parse** (`plan_parse.c`) — capability/provider blocks are
   shape-validated. Duplicates, malformed entries, and unknown
   `provider`/`access` values are rejected.
2. **App host** (`app_host.c`) — every capability with a `database`
   kind must reference a provider whose runtime feature is active.
   Mismatches abort startup.

After step 2, the request path can trust capability metadata. There's
no "best effort" — a Plan that doesn't pass both passes never gets to
serve traffic.

## Secret redaction

Two layers:

- **Plan-level**: the Plan parser rejects fields that look like
  credentials (matching configured patterns or explicitly tagged
  secrets). The compiler is expected to never emit them; this is
  defense in depth.
- **Provider-level**: every provider's `*.c` includes redaction
  helpers (e.g. `sl_pg_safe_config_hint` in `postgres.c`,
  equivalents in `sqlserver.c`). Connection strings, passwords, and
  parameter values are stripped from diagnostics by default.

Diagnostics still carry useful structure: error codes, SQLSTATE,
driver categories, source locations. Just no secret payloads.

The `ConfigSecretValue` wrapper at the JS layer (`stdlib/sloppy/internal/config.js`)
extends the same pattern to user-visible config — `secret.toString()`
returns `"[redacted]"`, only `secret.value()` exposes the real value.

## Resource handle isolation

JavaScript never gets a native pointer. Every native resource exposed
to JS goes through the resource table (`include/sloppy/resource.h`):

- A registration returns a generation-counted ID.
- The bridge looks up `(slot, generation, kind)` on every use.
- A stale ID — slot reused, kind wrong, table empty — fails cleanly.

This is what makes "JS can't get a `FILE*`" load-bearing instead of
aspirational.

## V8 bridge invariants (security-relevant)

From [v8-bridge.md](v8-bridge.md), the bits that matter for security:

- One owner thread per isolate. Off-thread JS access fails before V8
  sees it.
- Buffers are copied across the bridge. No shared memory between
  native and JS.
- C++ exceptions caught at the bridge. No exception machinery escapes
  to C.
- Source maps remap exception traces. JS errors point at original
  sources, not the generated bundle.

The bridge is small, named, and reviewable. Audit changes to
`src/engine/v8/` carefully — this is where the security boundary lives.

## TLS

Inbound TLS is opt-in OpenSSL plumbing in
`src/platform/libuv/http_transport_libuv.c`. Configure with:

```
Sloppy:Server:Tls:Enabled = true
Sloppy:Server:Tls:CertificatePath = path/to/cert.pem
Sloppy:Server:Tls:PrivateKeyPath  = path/to/key.pem
Sloppy:Server:Tls:Passphrase      = (passphrase or env reference)
```

Path validation rejects:

- relative paths that escape the artifact directory;
- paths with embedded NULs;
- missing certificate or key files;
- unreadable / oversized inputs.

Not implemented today: ALPN selection beyond HTTP/1.1, mTLS, custom
verification, OCSP stapling, HSTS hardening. Production deployments
should terminate TLS at a reverse proxy.

## What's not in scope

- **OS sandboxing.** Capabilities are policy declarations; they don't
  prevent native code or kernel bugs from misbehaving. If you need
  process isolation, that's the OS's job.
- **Authentication / authorization of end users.** Sloppy ships no
  auth stack today. Layer your own middleware/services or an API
  gateway. (On the framework capability roadmap.)
- **Encryption at rest.** Use the platform's secret store (Kubernetes
  secrets, AWS Secrets Manager, Vault) to inject values via
  environment variables.
- **Threat modeling for production deployment.** Pre-alpha. The
  bridge boundaries and redaction give you a reasonable foundation;
  production posture is your responsibility for now.

## Reporting issues

Don't open public issues for security findings. Use GitHub's private
vulnerability reporting on the repo, or email the maintainers (see
the repository profile). We acknowledge and coordinate disclosure.

## Tests

- **Plan validation negative tests** — every rejection path in
  `plan_parse.c` has a dedicated test.
- **Redaction goldens** under `tests/golden/diagnostics/` pin redacted
  diagnostic shapes; drift is a test failure.
- **V8 bridge boundary tests** verify that native pointers don't escape
  and that resource generation checks work.
- **TLS conformance** lanes (V8-gated, opt-in) cover certificate path
  validation and handshake behavior.

## See also

- [about/security.md](../about/security.md) — the user-facing model
- [Plan](plan.md) — validation order
- [Provider runtime](provider-runtime.md) — provider redaction
- [V8 bridge](v8-bridge.md) — bridge invariants
- [Memory model](memory-model.md) — resource handle isolation
