# Security and redaction

A backend runtime makes choices about what gets logged, what shows up in
diagnostics, and what flows across the JS/native boundary. This page
documents what Sloppy does today and what it doesn't.

## What Sloppy redacts by default

- **Configuration secrets.** Any value retrieved through
  `config.getSecret(key)` or marked `secret: true` in a config schema is
  wrapped — its `toString()` returns `"[redacted]"`. The wrapper exposes
  the real value only through `.value()`. Diagnostics, log fields, and
  the `audit` command never include the unwrapped value.
- **Provider credentials.** Connection strings configured through
  `Sloppy:Providers:<kind>:<name>:connectionString` are kept on the
  config side and never appear in Plan metadata or in default diagnostic
  output.
- **Diagnostic source contexts.** Diagnostic snippets show source lines
  but not the values of variables visible at that location — there's no
  "captured locals" output.

## What Sloppy doesn't try to do

- **Sandbox the process.** Capabilities are policy declarations the
  runtime checks before opening resources, not OS-level isolation. Code
  that wants to read `/etc/passwd` from JavaScript can't (the JS API
  surface doesn't expose it), but C-level vulnerabilities or Sloppy
  bridge bugs are not a sandbox.
- **Authenticate or authorize end users.** Sloppy ships no auth stack
  today. Add your own middleware/services or place an API gateway in
  front. (Auth is on the framework capability roadmap; treat anything
  here as your job for now.)
- **Encrypt secrets at rest.** Config files are plaintext. Use your
  platform's secret store (Kubernetes secrets, AWS Secrets Manager,
  Vault) and inject through environment variables that Sloppy resolves
  at startup.

## The V8 bridge as a security boundary

The bridge ensures:

- JavaScript code can't get a raw native pointer.
- JS-visible buffers are copies, not shared memory aliased by the kernel.
- Promise lifetimes are bounded by the owner thread; long-running awaits
  can't keep the runtime alive after shutdown.

These invariants are part of why every JS-visible buffer involves a copy.
That trade is intentional.

## TLS

Inbound TLS exists as opt-in plumbing through OpenSSL. Configure it via
`Sloppy:Server:Tls:Enabled`, `:CertificatePath`, and `:PrivateKeyPath`. Out
of scope today: ALPN selection beyond HTTP/1.1, mTLS, custom certificate
verification, and HSTS hardening. For production you'll generally want a
reverse proxy in front handling TLS termination.

## Reporting issues

If you find a security issue, please don't open a public GitHub issue.
Email the maintainers (see the repository profile) or use GitHub's
private vulnerability reporting on the repo. We'll acknowledge and
coordinate disclosure from there.
