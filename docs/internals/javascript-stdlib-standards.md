# JavaScript Stdlib Standards

Sloppy's JavaScript stdlib is runtime code. It is not sample glue, frontend
code, or a place for compatibility theater. Keep it small, explicit, testable,
and honest about alpha boundaries.

## Module Boundaries

Public modules under `stdlib/sloppy/*.js` export stable API shape. Internal
modules under `stdlib/sloppy/internal/*.js` may change and should hold reusable
implementation primitives. A public module may be a small facade over internal
logic when that keeps the public API legible. Do not create god modules unless
the file is intentionally a public aggregation point such as `index.js`.

Internal helpers must have one clear purpose. Use focused modules such as
`validation.js`, `redaction.js`, and `disposable.js`; do not build a junk drawer
of unrelated convenience functions.

## Function Shape

Prefer functions with one responsibility. Split an 80-line validation or
lifecycle block when named helpers make the flow easier to review. Do not split
two-line logic into one-use abstractions that hide important side effects. Remove
large defensive blobs around impossible states instead of preserving ceremony.

## Validation

Validate public API inputs once at the boundary: registration, configuration,
builder mutation, or descriptor creation. Hot request paths should consume
normalized descriptors and avoid revalidating static configuration. Reuse
internal validation helpers for plain-object checks, integer bounds, non-empty
strings, and HTTP tokens so error behavior stays consistent.

## Errors And Diagnostics

Auth, providers, TestHost, TestServices, cache, jobs, HTTP client, and other
security or lifecycle surfaces fail closed. User-visible errors must use stable
`SLOPPY_E_*` codes where the module exposes coded errors. Use `TypeError` for
programmer misuse and configuration shape mistakes.

Never swallow errors with `catch (() => {})` or equivalent unless the reason is
documented in code and covered by a test. Diagnostics must be direct, bounded,
and redacted.

## Security And Redaction

Do not place raw secrets in errors, diagnostics, logs, snapshots, or examples.
Redact Authorization, Cookie, API key, password, token, secret, Redis URL, and
database connection-string values. Payload previews must be bounded. Prefer the
shared redaction helpers and module-specific wrappers for provider connection
strings.

## Hot Paths

Move setup work out of request and message paths. Avoid repeated regex
construction, repeated deep clone, repeated object spread, JSON stringify/parse
cloning, throwaway closures in obvious loops, and unnecessary async wrappers on
success paths. Do not optimize into unreadable code; use benchmarks only when
they prove a real path.

## Immutability

Freeze public descriptors and snapshots that cross API boundaries. Do not
deep-freeze every temporary object blindly. Keep descriptor lifecycle separate
from runtime instance lifecycle, especially for services, providers, TestHost,
jobs, cache, HTTP clients, and realtime channels.

## Async And Disposal

Cleanup must run on success, failure, timeout, cancellation, startup failure, and
handler failure. `dispose()` must be idempotent. Use `Symbol.asyncDispose` where
the module already supports it and it clarifies ownership. Cleanup failures must
remain visible; aggregate them when multiple owned resources can fail.

## Runtime-Classic And Embedded JS

Source stdlib modules and `stdlib/sloppy/internal/runtime-classic.js` must not
silently diverge. When a source API is embedded, transformed, or copied, add a
test for expected exports and minimal behavior in the embedded variant. Stringly
module rewrites need tests for import/export removal and expected symbols.

## Testing

Every public behavior needs tests. Internal helpers need focused bootstrap tests
when they become shared contract. TestHost/TestServices, auth, providers, cache,
HTTP client, jobs, WebSocket/realtime, and runtime-classic paths require negative
tests for lifecycle, redaction, and failure behavior when touched.

Node-based ESM bootstrap tests are execution coverage for the source stdlib, not
proof that the V8 bridge can load ESM modules. Keep that boundary explicit.

## Dependency Policy

Do not add npm packages for simple validation, parsing, redaction, disposal,
formatting, or style enforcement. Use Sloppy-owned primitives and existing repo
tooling.

## Anti-Patterns

- Large defensive blob.
- Copy-pasted helper.
- Stringly module rewrite without tests.
- Fake fallback that reports success without doing the work.
- `catch (() => {})` without a diagnostic and test.
- Hidden singleton mutable state.
- Public example using unsupported Node-only semantics.
