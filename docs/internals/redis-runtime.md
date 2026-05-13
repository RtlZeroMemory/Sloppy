# Redis Runtime

The Redis runtime surface is implemented in JavaScript stdlib code and uses the
Sloppy outbound network bridge. There is no third-party Redis npm client in the
runtime package.

## Ownership

- `stdlib/sloppy/redis.js` owns RESP2 encoding/parsing, connection setup,
  pooling, command helpers, script helpers, lock helpers, metrics, diagnostics,
  and redaction.
- `stdlib/sloppy/cache.js` owns the Redis-backed cache provider.
- `stdlib/sloppy/testservices.js` owns Docker-backed Redis test service startup
  and readiness.
- `stdlib/sloppy/internal/runtime-classic.js` carries the classic-script copy
  used by V8 artifact execution.
- The Rust compiler marks `stdlib.redis`, `stdlib.cache`, and `stdlib.net`
  feature requirements when imports make those dependencies visible.

## Bridge Boundary

Redis connects through:

- `globalThis.__sloppy.net.connect` for `redis://`;
- `globalThis.__sloppy.net.connectTls` for `rediss://`.

The Redis stdlib code does not expose native socket handles to JavaScript. The
network bridge owns the platform-specific TCP/TLS implementation and deadline
behavior. Redis reports a clear unavailable-runtime-feature error when the
bridge is absent.

Package directories carry the generated artifacts, not a Redis transport by
themselves. Redis package-runtime support is therefore limited to V8-enabled
runtime lanes where `__sloppy.net.connect` is active and a live Redis service is
provided. The default non-V8 package lane remains an unsupported Redis runtime
lane and should report the V8-required diagnostic rather than a fake cache or
memory fallback.

## Protocol Boundary

The first implementation intentionally uses RESP2. The parser accepts simple
strings, errors, integers, bulk strings, null bulk strings, and arrays. It
rejects malformed frames, excessive bulk lengths, and excessive nesting.

Pipeline support writes multiple encoded RESP commands to one leased
connection and reads the same number of replies. Command helpers use arrays of
arguments; callers do not write raw protocol bytes.

## Operational Policy

Connection pools are bounded. When all connections are busy, callers wait in a
bounded queue or fail with a Redis timeout error. Idle connections are closed
after the configured idle timeout.

Metrics are low-cardinality and never label by Redis key, URL, password, token,
or command arguments. Diagnostics return redacted endpoint state and pool
counts only.

## TestServices Redis

`TestServices.redis()` starts a real `redis:7-alpine` container through the
Docker CLI, waits for Redis readiness through the first-party Redis client, and
returns a disposable service with `url`, `env()`, `client()`, `flush()`,
`reset()`, `logs()`, `diagnostics()`, and `dispose()`.

Readiness is not faked. Docker unavailability, network bridge unavailability,
startup timeout, and cleanup failures are reported distinctly.

## Non-Goals

- No npm Redis client dependency.
- No in-memory Redis replacement.
- No RESP3, cluster, sentinel, pub/sub, streams, modules, or client-side cache
  contract.
- No public raw socket or native Redis handle.
- No production-ready operational pooling policy claim.
