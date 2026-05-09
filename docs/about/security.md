# Security and redaction

A backend runtime makes choices about what gets logged, what shows up in
diagnostics, and what flows across the JS/native boundary. This page
documents what Sloppy does today and what it doesn't.

## What Sloppy redacts by default

- **Configuration secrets.** Values retrieved through
  `config.getSecret(key)` or marked `secret: true` in a config schema
  are wrapped. The wrapper's `toString()` returns
  `"[Secret redacted]"`; the real value is only available via
  `secret.value()`. Default diagnostics, audit output, and Plan
  metadata don't include the unwrapped value. Handler code can still
  leak the value if it unwraps it and logs the result — the wrapper
  redacts by default but doesn't enforce.
- **Provider credentials.** Connection strings passed to
  `data.<provider>.open({ connectionString })` are kept on the config
  side and don't appear in Plan metadata or in default diagnostic
  output.
- **TLS material references in diagnostics.** Diagnostic redaction
  treats TLS certificate, private-key, client-certificate, and CA
  bundle, and trust-store path keys as sensitive. Current Plan
  metadata still carries inbound server certificate and private-key
  paths so `sloppy run` can configure the development TLS listener.
- **Diagnostic source contexts.** Diagnostic snippets show source
  lines but not the values of variables visible at that location —
  there's no "captured locals" output.

## What Sloppy doesn't try to do

- **Sandbox the process.** Capabilities are policy declarations the
  runtime checks before opening resources, not OS-level isolation.
  Sloppy *does* expose filesystem, network, OS-process, and similar
  APIs to JavaScript — they're feature-gated through the Plan and the
  V8 intrinsics, but once enabled they perform real OS calls. Treat
  capability checks as policy, not a sandbox; if you need process
  isolation, that's the OS's job.
- **Authenticate or authorize end users.** Sloppy ships no auth stack
  today. Implement auth as handlers or services that run before the
  protected work, or terminate auth at an upstream API gateway.
  Middleware/endpoint filters are upcoming framework work — once they
  land, auth filters will be the natural shape, but they don't exist
  yet.
- **Encrypt secrets at rest.** Config files are plaintext. Use your
  platform's secret store (Kubernetes secrets, AWS Secrets Manager,
  Vault) and inject through environment variables read via
  `Environment.get(...)` from `"sloppy/os"`.

## The V8 bridge as a security boundary

The bridge ensures:

- JavaScript code can't get a raw native pointer.
- JS-visible buffers are copies, not shared memory aliased by the kernel.
- Promise lifetimes are bounded by the owner thread; long-running awaits
  can't keep the runtime alive after shutdown.

These invariants are part of why every JS-visible buffer involves a copy.
That trade is intentional.

## TLS

Inbound TLS exists as opt-in plumbing through OpenSSL. Configuration keys live
under the `sloppy:server:tls:*` namespace (`enabled`, `certificatePath`,
`privateKeyPath`); environment variable form is
`SLOPPY__SERVER__TLS__ENABLED`, etc.

Outbound `HttpClient` HTTPS/TLS is experimental. It uses OpenSSL through a
private V8 bridge when that bridge is present. The public API accepts
trust-store and client-certificate path options, but those option names may
still change before a broader stability contract exists. Diagnostics redact
those paths and any private-key passphrase.

Out of scope today: ALPN selection beyond HTTP/1.1, server-side mTLS,
custom certificate verifier callbacks, OCSP stapling, and HSTS hardening. For
production you'll generally want a reverse proxy in front handling TLS
termination.

## Reporting issues

If you find a security issue, please don't open a public GitHub issue.
Email the maintainers (see the repository profile) or use GitHub's
private vulnerability reporting on the repo. We'll acknowledge and
coordinate disclosure from there.
