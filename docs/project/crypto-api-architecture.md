# Crypto API Architecture

Status: CORE-CRYPTO-01.A/B contract. This document defines the intended
`sloppy/crypto` surface and backend policy before primitive implementation. It is not
implementation evidence for random quality, hash throughput, password cost behavior, or V8
runtime execution.

## Goals

`sloppy/crypto` gives Sloppy apps a compact runtime-owned API for common backend crypto
tasks:

- secure random values, UUIDs, tokens, hex strings, and numeric codes;
- SHA-2 hashing and HMAC signing/verification;
- password hashing, verification, and rehash policy;
- constant-time equality helpers;
- owned `Secret` buffers with explicit disposal;
- explicitly separated non-cryptographic hash helpers.

Sloppy owns the public API shape, feature model, diagnostics, V8 bridge boundaries,
ownership/lifecycle behavior, and examples. Cryptographic primitives come from OS
facilities or vetted libraries. Sloppy must not implement custom crypto algorithms.

## Public Module

The public import is:

```ts
import {
  Random,
  Hash,
  Hmac,
  Password,
  ConstantTime,
  Secret,
  NonCryptoHash
} from "sloppy/crypto";
```

The compiler recognizes only named, unaliased imports from `sloppy/crypto`. The import
activates the `stdlib.crypto` runtime feature in emitted Plan metadata. The current
contract PR keeps `stdlib.crypto` unavailable by default until the implementation PRs
register real backends and V8 intrinsics.

## API Contract

`Random`:

- `Random.bytes(length)` returns secure random bytes.
- `Random.uuid()` returns a version 4 UUID string.
- `Random.token(length)` returns a URL-safe random token.
- `Random.hex(length)` returns random bytes encoded as lowercase hex.
- `Random.numericCode(length)` returns decimal digits generated without modulo bias.

`Hash`:

- `Hash.sha256(data)`, `Hash.sha384(data)`, and `Hash.sha512(data)` return digest bytes.
- `Hash.sha256Hex(data)` and `Hash.sha256Base64(data)` return encoded SHA-256 digests.
- Equivalent SHA-384/SHA-512 encoded helpers may be added with the same naming pattern.
- `Hash.create("sha256" | "sha384" | "sha512")` returns an incremental hasher with
  `update(data)` and `digest(encoding?)`.
- Incremental hashers reject update-after-digest and double-digest deterministically.

`Hmac`:

- `Hmac.sha256(secret, data)`, `Hmac.sha384(secret, data)`, and `Hmac.sha512(secret, data)`
  return signature bytes.
- Encoded helpers follow the same `Hex`/`Base64` suffix pattern as `Hash`.
- `Hmac.verifySha256(secret, data, signature)` verifies with constant-time comparison.

`Password`:

- `Password.hash(password, options?)` returns an encoded password hash.
- `Password.verify(password, encodedHash)` returns a boolean.
- `Password.needsRehash(encodedHash, policy?)` reports whether the encoded hash should be
  upgraded to current defaults.
- Public password hashing is async. There is no synchronous public password hashing API.

`ConstantTime`:

- `ConstantTime.equals(a, b)` compares bytes in constant-time with respect to byte content.
  It is an equality helper, not timing-proof evidence.

`Secret`:

- `Secret.fromUtf8(value)` and `Secret.fromBytes(bytes)` create owned secret buffers.
- `secret.dispose()` performs cleanup once.
- Use after disposal reports a stale/disposed diagnostic and never prints the secret.

`NonCryptoHash`:

- Non-security hashes live only under `NonCryptoHash`.
- `NonCryptoHash.xxHash64(data)` is the selected initial API when the dependency lands.
- Non-crypto helpers must not appear in security examples and must not be described as
  password, MAC, signature, token, or integrity primitives.

## Algorithm Matrix

| Category | Algorithms | Status | Policy |
| --- | --- | --- | --- |
| Secure random | OS CSPRNG | Supported | Required for all `Random` APIs. No weak fallback. |
| UUID | UUID v4 | Supported | Generated from OS CSPRNG bytes with correct version/variant bits. |
| Token/code | URL-safe token, hex, decimal code | Supported | Rejection sampling where alphabet size could introduce modulo bias. |
| Hash | SHA-256, SHA-384, SHA-512 | Supported | OS crypto where sane, otherwise vetted dependency backend. |
| HMAC | HMAC-SHA-256, HMAC-SHA-384, HMAC-SHA-512 | Supported | Same backend family as SHA-2; verification uses constant-time compare. |
| Password | Argon2id PHC | Supported target | Default once `libargon2` or equivalent vetted backend is integrated. |
| Password compatibility | bcrypt | Deferred | Compatibility only if a vetted dependency and safe bounds are selected. |
| Password legacy | PBKDF2 | Deferred | Interoperability only; never default for new hashes. |
| Legacy crypto | MD5, SHA-1, weak ciphers | Rejected for secure APIs | May only appear as explicit warning/deferred compatibility policy. |
| Non-crypto hash | xxHash64 | Supported target | Dependency-backed and visibly separate from `Hash`/`Hmac`. |
| BLAKE3 | Deferred | Not selected for this EPIC until use cases and dependency policy are scoped. |

## Backend Policy

Random uses platform OS facilities through `src/platform/*` only:

- Windows: CNG, such as `BCryptGenRandom`.
- Linux: `getrandom`/`getentropy` or an equivalent secure kernel source through the
  platform abstraction.
- macOS: `SecRandomCopyBytes` or the platform-approved secure source.

Hash/HMAC use stable OS crypto where the platform provides a suitable API. Linux may use
OpenSSL 3 through the dependency toolchain because there is no single portable kernel hash
API. Backend selection must stay behind Sloppy-owned C interfaces; public headers and JS
must not expose OpenSSL, CNG, Security.framework, or provider-native types.

Password hashing uses a vetted dependency. The selected default is Argon2id with PHC
encoded output, safe memory/time/parallelism defaults, and documented upper/lower bounds.
`bcrypt` and `PBKDF2` remain compatibility/deferred work unless a later PR explicitly
selects and tests them.

Non-cryptographic hashing uses a vetted `xxhash` dependency or equivalent approved
implementation. It must not reuse the existing internal `SlStr`/`SlBytes` deterministic
hash helpers as a public algorithm contract.

## Async and Owner-Thread Policy

Small bounded hash/HMAC work may run inline only when a later implementation PR documents
the size cap and evidence. Large hash/HMAC work should offload when it can otherwise block
the V8 owner thread.

Password hashing and verification are always admitted to an offload path. Password work
must copy cross-thread inputs into owned buffers, clean up exactly once, and settle
Promises on the V8 owner thread. Late completion after cancellation or shutdown is
cleanup-only.

## Feature and Plan Model

Feature id: `stdlib.crypto`.

Public import: `sloppy/crypto`.

Private V8 intrinsic namespace: `__sloppy.crypto`.

Dependencies: `core` and `v8`.

Compiler behavior:

- `sloppy/crypto` named imports add `stdlib.crypto` to Plan `requiredFeatures[]`.
- The compiler emits `features.crypto = true`.
- The strong Plan evidence block emits `strongPlan.evidence.crypto = true`.
- Future PRs may add statically visible metadata for password hashing, HMAC, non-crypto
  hash use, and legacy/insecure algorithm warnings where the compiler can see them.

Runtime behavior in this PR:

- `stdlib.crypto` is known to the feature registry.
- Default availability is false until native and V8 implementations land.
- Required `stdlib.crypto` fails closed with runtime-feature diagnostics.

## Diagnostics

Stable crypto diagnostics:

- `SLOPPY_E_CRYPTO_FEATURE_UNAVAILABLE`
- `SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM`
- `SLOPPY_E_CRYPTO_INSECURE_LEGACY_ALGORITHM`
- `SLOPPY_E_CRYPTO_INVALID_KEY_SECRET`
- `SLOPPY_E_CRYPTO_PASSWORD_VERIFY_FAILED`
- `SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED`
- `SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE`
- `SLOPPY_E_CRYPTO_SECRET_DISPOSED`
- `SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT`
- `SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE`

Diagnostics may name operation, algorithm id, backend family, and shape constraints. They
must not include secret bytes, passwords, encoded-hash internals, raw native pointers, V8
handles, provider handles, random output, or package-manager state.

## Redaction and Zeroization

Secret-bearing values are redacted before diagnostics or Plan/tooling output. The shared
redaction helper recognizes common password/token/key/secret labels, connection strings,
URI user-info passwords, and crypto-specific labels such as `clientSecret`, `private_key`,
`secret_key`, and `passphrase`.

`Secret` disposal is best-effort zeroization of Sloppy-owned native buffers. It cannot
erase prior JavaScript string copies, engine internals, operating-system paging, crash
dumps, or caller-owned buffers. API docs and examples must describe that limitation
plainly.

## Evidence Boundaries

Deterministic tests may prove API shape, validation, diagnostics, Plan activation,
standard vectors, and lifecycle cleanup. They must not claim randomness quality,
performance, resistance to side channels, password cracking cost, or production security
hardening.

Evidence lanes remain separate:

- default non-V8 lane;
- V8-gated lane;
- package lane;
- live-provider lane;
- stress/torture lane;
- benchmark lane.

This EPIC does not add WebCrypto compatibility, Node crypto compatibility, Bun
compatibility, TLS/certificate APIs, package-manager behavior, public alpha docs,
performance claims, custom crypto algorithms, weak random fallback, or unrelated
network/process/filesystem behavior.
