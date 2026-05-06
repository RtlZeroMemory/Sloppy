# Core API Platform Map

Status: CORE-INTEGRATION-01 source of truth. This document records the current Core API
interop rules after the consolidation pass for #643 and child issues #644-#653. It is an
internal architecture map, not a public readiness statement.

## Platform Shape

Sloppy Core APIs are one runtime platform made from layered primitives:

- Native core primitives: status, arena, string, bytes, builders, checked math,
  diagnostics, cancellation tokens, feature descriptors, capability metadata, and app
  lifecycle/resource ownership.
- Platform backends: OS, filesystem, process, libuv transport, local IPC, and provider
  backends under `src/platform/*`.
- V8 bridge: JS-facing intrinsics under `src/engine/v8/*`; V8 types stay there.
- Stdlib modules: `sloppy/fs`, `sloppy/time`, `sloppy/codec`, `sloppy/crypto`,
  `sloppy/net`, `sloppy/os`, `sloppy/workers`, config/app helpers, and provider facades.
- Compiler/Plan tooling: Rust-owned source and metadata tooling; it does not depend on
  runtime C APIs.

Bootstrap/runtime artifact reads are internal host reads. They are separate from public
`sloppy/fs` application policy and must stay available even when app-facing filesystem
policy is strict or denied.

## Dependency Rules

Allowed low-to-high direction:

- Time depends only on core/V8 bridge support for the JS API.
- Codec depends only on core/V8 bridge support and optional codec backends.
- Crypto depends on core/V8 bridge support and may use Codec formatting/text helpers.
- FS may depend on Time for JS option behavior, Codec for JS text helpers, and native
  filesystem/platform backends.
- Net/local IPC may depend on Time, transport backends, and Codec for JS text helpers.
- HTTP client may depend on Net, Time, Codec, Crypto/redaction, and transport backends.
- OS/process may depend on Time, platform process backends, and Codec for JS pipe text
  helpers.
- Workers may depend on Time, Codec, and worker/offload backends.
- Config may depend on internal runtime file access, OS env access, Time duration parsing,
  and Crypto/Secret redaction helpers where those features are active.

Forbidden direction:

- Time must not depend on FS, Network, HTTP, Process, Crypto, Codec, Workers, Config, or
  app/framework code.
- Codec must not depend on Network, HTTP, Process, Workers, Config, or app/framework code.
- Crypto must not depend on HTTP, Network, Process, Workers, Config, or provider APIs.
- Diagnostics must not depend on high-level FS/Network/Process/HTTP/Worker APIs.
- Platform headers and OS/libuv types must not escape `src/platform/*`.
- V8 types must not escape `src/engine/v8/*`.
- Public JS APIs must receive Slop-owned resources, opaque IDs/objects, or value objects,
  not raw native pointers, OS handles, sockets, process handles, worker handles, TLS
  handles, codec handles, or provider handles.

Suspicious dependencies that require review:

- Core headers that pull in app/framework or transport internals.
- V8 intrinsics that read caller-controlled paths directly instead of using an app/root
  policy or an explicit runtime artifact loader.
- Public include structs that expose mutable backend state instead of opaque handles.
- Provider-specific redaction or text helpers that duplicate shared redaction/Codec
  behavior without a documented grammar exception.

## Primitive Ownership

| Primitive | Canonical owner | Current adopters |
| --- | --- | --- |
| Deadlines, timeout budget, cancellation classes | `sloppy/time`, native `SlCancellationToken` | FS, local IPC, HTTP client, OS/process preflight, Workers |
| ASCII byte/string equality and prefix/suffix | `sloppy/string.h` | HTTP dispatch/backend/response, libuv HTTP transport, diagnostics secret scanning, provider connection-string parsing |
| UTF-8 text encode/decode | `sloppy/codec` `Text.utf8` | Crypto string inputs, FS handle text/lines, Net text/body helpers, OS pipe text/lines |
| Base64, Base64Url, Hex | `sloppy/codec` | Crypto hash formatting and Secret/string data conversion |
| Secure random/token, Secret values, constant-time comparison | `sloppy/crypto` | Crypto stdlib; other APIs must use these instead of local security helpers |
| Redaction | Crypto/Secret and diagnostics redaction helpers | Config secrets, provider metadata, doctor/audit output, diagnostics/goldens |
| Diagnostics and stable codes | `include/sloppy/diagnostics.h`, `src/core/diagnostics.c` | Native core, V8 bridge, stdlib errors, doctor/audit |
| Resource lifecycle | app lifecycle/resource table and per-family handles | FS handles/watchers, Net/TCP/local IPC, HTTP request/response, OS processes, Workers |

## Canonical Option Shape

Core APIs that accept operation timing use this shape:

```js
{
  timeoutMs, // operation-local budget
  deadline,  // shared Deadline budget
  signal     // caller-owned cancellation signal
}
```

Rules:

- `timeoutMs` creates an operation-local deadline and must be finite, non-negative, and
  bounded by the backend type when a native backend requires a narrower range.
- `deadline` is a shared budget object with `remainingMs()`. Expired deadlines fail before
  bridge admission when JS can observe them.
- `signal` is external cancellation. Pre-aborted signals must produce cancellation, not
  timeout.
- When both `signal` and deadline/timeout are present, the terminal reason that wins first
  is preserved. Timeout and cancellation remain distinguishable.
- Non-interruptible native operations must not pretend work stopped. If JS timeout or
  cancellation wins first, late native completion is cleanup-only and must not double-settle.
- Cleanup must run once across success, error, cancellation, timeout, dispose, and shutdown.

Current conformance:

- FS uses Time wrappers for operation budget and cancellation preflight. Late cleanup for
  resource-producing open/watch is still deferred.
- Local IPC normalizes timeout/deadline/signal and cleans up late connect/listen/accept
  handles. Per-read/write native cancellation tokens are still bridge-limited.
- HTTP client normalizes request timeout/deadline/signal, keeps timeout/cancel precedence
  over late transport errors, and now closes request-body iterators on terminal exits.
- OS/process supports deadline/signal preflight and timeout mapping. Live post-admission
  process cancellation remains deferred.
- Workers normalize timeout/deadline/signal, reject invalid signal shapes, and backpressure
  waiters now observe caller timeout/cancellation before job admission.

## Streams, Backpressure, and Ownership

Byte streams move owned `Uint8Array` chunks. Text helpers decode through Codec; embedded NUL
bytes are preserved in byte APIs and decoded text. APIs must document EOF, close, error,
cancel, stale resource, and dispose behavior.

Conventions:

- `readChunks()` yields owned chunks and should end cleanly on EOF where the backend can
  distinguish EOF from stale/disposed resources.
- `readLines()` decodes with Codec UTF-8, preserves line boundaries, and enforces line
  length per emitted line and final tail rather than aggregate chunk text.
- Writers accept `Uint8Array` for binary correctness; `writeText()` uses Codec UTF-8.
- Queues and streams must have explicit bounds. Hidden unbounded buffers are defects.
- Cross-thread data is copied or transferred with explicit ownership. Borrowed views must
  not outlive their owner.

Current conformance:

- FS handle text/line helpers use Codec UTF-8 and per-line `maxLineLength`.
- Net local IPC and HTTP client text/body helpers use Codec UTF-8.
- HTTP request stream cleanup calls iterator `return()` on timeout, cancellation, invalid
  chunks, and body-limit failures.
- Workers backpressure waiters are bounded and now observe deadline/cancellation.

Deferred stream work:

- V8 process pipe reads still need binary-first native return coverage.
- TCP `readLine`/`readUntil` need shared `maxBytes`, closed-handle preflight, and
  timeout/deadline/signal normalization.
- Local IPC/TCP async iteration needs EOF-as-done coverage where native backends can
  distinguish peer EOF from stale handles.

## Diagnostics And Redaction

Representative categories must have stable diagnostics and redacted output:

- feature unavailable
- policy denied
- invalid option
- timeout
- cancellation
- disposed/stale resource
- backend unavailable
- unsupported platform
- malformed input
- overflow/limit exceeded
- secret redacted
- resource shutdown/app shutdown
- late completion cleanup-only

Rules:

- Native diagnostics own stable taxonomy and rendering.
- JS family errors should expose stable `code` when they emit `SLOPPY_E_*` or `SLOPPY_W_*`
  codes. `SloppyNetError` now derives `.code` from stable message prefixes.
- Doctor/audit output must preserve enough metadata to debug without printing secrets.
- Headers, tokens, env values, connection strings, passwords, private keys, passphrases,
  provider credentials, encoded password hashes, and package/golden outputs are
  security-sensitive.
- Deterministic tests may verify formatting and redaction, but must not claim randomness,
  security strength, or performance.

## Duplicate Helper Inventory

Removed or adopted in this pass:

- Native ASCII case-insensitive helpers in HTTP dispatch/backend/response/libuv transport,
  diagnostics secret scanning, and SQL Server provider parsing now use
  `sl_str_equal_ci_ascii`, `sl_str_starts_with_ci_ascii`, and `sl_str_ends_with_ci_ascii`.
- `sloppy/crypto` hash formatting and string-to-bytes conversion now use Codec
  `Hex`, `Base64`, and `Text.utf8`.
- FS, Net, HTTP client, and OS text helpers use Codec `Text.utf8` instead of direct
  `TextEncoder`/`TextDecoder`.
- HTTP request stream cleanup and Worker backpressure timing now use shared terminal-state
  expectations instead of local mini-semantics.

Documented exceptions:

- `stdlib/sloppy/internal/runtime-classic.js` is a self-contained bootstrap bundle and still
  duplicates portions of ESM stdlib behavior. It must be covered by parity/regeneration
  checks before broad edits.
- JSON escaping, HTTP chunk framing, compiler artifact hashing, and provider-specific
  connection string grammars are format/protocol-specific helpers, not generic Codec/Crypto
  API replacements.
- Platform backends may have OS-specific path/text adapters under `src/platform/*`.

Mechanical enforcement:

- `tools/windows/check-core-api-integration.ps1` and
  `tools/unix/check-core-api-integration.sh` reject direct `TextEncoder`/`TextDecoder` use
  outside Codec/runtime-classic, local JS UTF-8/hex/base64 helper reintroductions, local
  native ASCII comparison helper reintroductions, and `Math.random` use in public stdlib
  runtime paths.
- The Windows scanner is part of `tools/windows/dev.ps1 lint`.

## Policy And Strict Mode

Current implemented behavior:

- Development mode remains ergonomic and documented as a local developer convenience.
- Strict mode must fail closed for policy-sensitive operations where enforcement exists.
- HTTP client strict outbound policy is active in the JS client path.
- FS core policy primitives and diagnostics exist.
- Config secret values and provider metadata are redacted in diagnostics and doctor/audit
  paths.
- Runtime/bootstrap artifact reads stay independent from public app FS policy.

Deferred policy work:

- Real `sloppy run` V8 execution must pass app-facing strict FS policy into
  `SlEngineOptions` instead of falling back to development roots.
- Raw TCP connect/listen and OS process run/kill/signal need shared strict policy gates.
- Worker module source loading needs an explicit app artifact/root policy and path escape
  denial tests.
- Doctor/audit metadata parsing should fail closed on malformed known fields instead of
  silently treating malformed booleans as false.

## Evidence Lanes

Default required lanes for this map:

- Native unit/integration tests.
- Bootstrap stdlib tests, including `bootstrap.stdlib.core_integration`.
- Example API-shape tests, including `examples.core_integration.api_shape`.
- Platform boundary check.
- C, JS/TS, and Rust standards checks.
- Core API integration scanner.
- `git diff --check`.

Separate optional or gated lanes:

- V8-enabled Windows lane.
- Compiler/Plan lane.
- Source-input/package/bundle lane.
- Platform-specific lanes.
- Live-network/provider lanes.
- Fuzz/property lanes.
- Stress/torture lanes.
- Benchmark lanes.

Skipped optional lanes are not pass evidence. This document makes no benchmark,
production-readiness, public alpha, or Node/Bun/Deno compatibility claim.
