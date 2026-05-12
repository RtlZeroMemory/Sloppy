# Crypto

`sloppy/crypto` is the bootstrap stdlib cryptographic surface. It wraps
platform-provided primitives — OS CSPRNG, SHA-2 family hashes, HMAC,
constant-time comparison, Argon2id password hashing, and a separate
non-cryptographic xxHash64 — behind frozen JS facades.

## Import

```ts
import {
    Random,
    Hash,
    Hmac,
    ConstantTime,
    Password,
    Secret,
    NonCryptoHash,
} from "sloppy/crypto";
```

The compiler recognizes `sloppy/crypto` as a stdlib subpath and emits the
`stdlib.crypto` runtime feature.

## Current status

This public alpha, pre-production API shape is committed for current experiments. Every
operation requires the
`__sloppy.crypto` bridge; without it the call rejects with
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE`. Sloppy doesn't ship its own
cryptographic primitives in JS — these helpers route to the platform
implementation.

## Random

`Random` is a frozen namespace for OS-CSPRNG-backed randomness.

| Method | Returns | Limits |
| --- | --- | --- |
| `Random.bytes(length)` | `Uint8Array` | `length` 0..4096 |
| `Random.uuid()` | `string` | UUID v4 |
| `Random.token(length)` | `string` | base64url alphabet (`A-Za-z0-9-_`), 0..1024 |
| `Random.hex(length)` | `string` | hex string, output length is `2*length`, length 0..1024 |
| `Random.numericCode(length)` | `string` | digits 0–9, 0..1024 |

All five are synchronous and return immediately when the bridge is present.

## Hash

`Hash` covers SHA-2 digests over strings, byte arrays, or `Secret` values.

| Method | Returns |
| --- | --- |
| `Hash.sha256(value)` | `Promise<Uint8Array>` |
| `Hash.sha384(value)` | `Promise<Uint8Array>` |
| `Hash.sha512(value)` | `Promise<Uint8Array>` |
| `Hash.sha256Hex(value)` | `Promise<string>` |
| `Hash.sha256Base64(value)` | `Promise<string>` |
| `Hash.create(algorithm)` | `IncrementalHasher` |

`algorithm` is `"sha256"`, `"sha384"`, or `"sha512"`. `IncrementalHasher`:

```ts
const hasher = Hash.create("sha256");
hasher.update("event:").update(payload);
const hex = await hasher.digest("hex");           // "hex" | "base64" | "bytes"
```

Inputs are bounded at 1 MiB per call. Larger inputs fail before reaching the
bridge. A hasher cannot be reused after `digest()`.

Digest sizes: SHA-256 = 32 bytes, SHA-384 = 48, SHA-512 = 64.

## Hmac

```ts
await Hmac.sha256(secret, value);                              // Promise<Uint8Array>
await Hmac.sha384(secret, value);                              // Promise<Uint8Array>
await Hmac.sha512(secret, value);                              // Promise<Uint8Array>
await Hmac.verifySha256(secret, value, signature);             // Promise<boolean>
await Hmac.verifySha384(secret, value, signature);             // Promise<boolean>
await Hmac.verifySha512(secret, value, signature);             // Promise<boolean>
```

`secret`, `value`, and `signature` accept `string`, `Uint8Array`, or
`Secret`. `verifySha256`, `verifySha384`, and `verifySha512` perform the
comparison in constant time.

## ConstantTime

```ts
ConstantTime.equals(left, right): boolean;
```

Synchronous and constant-time over the lengths of the inputs. Use it whenever
you compare an attacker-controlled value against a secret.

## Password

`Password` wraps Argon2id password hashing. Operations are async because the
runtime offloads them to a worker thread.

```ts
const encoded = await Password.hash(password, options?);
const ok = await Password.verify(password, encoded);
const stale = await Password.needsRehash(encoded, options?);
```

Options:

```ts
{
  opsLimit?: number;          // 2..4, default 2
  memoryLimitBytes?: number;  // 64 MiB..256 MiB, default 64 MiB
}
```

Inputs are bounded at 4 KiB. The encoded hash is the standard Argon2id
encoded string (≤128 bytes).

## Secret

`Secret` is the storage type for sensitive bytes. It clones data on
construction and zeroes its buffer on `dispose()`. `toString()` and
`toJSON()` always emit `"[Secret redacted]"`.

```ts
import { Secret, Hmac } from "sloppy/crypto";

const key = Secret.fromUtf8(configValue);
try {
    const signature = await Hmac.sha256(key, payload);
} finally {
    key.dispose();
}
```

| Factory / method | Effect |
| --- | --- |
| `Secret.fromUtf8(string)` | UTF-8 encode and store |
| `Secret.fromBytes(Uint8Array)` | clone and store |
| `secret.bytes()` | returns a clone; throws after `dispose()` |
| `secret.dispose()` | zero internal buffer; idempotent |
| `secret.toString()` / `toJSON()` | `"[Secret redacted]"` |

`Hash`, `Hmac`, `ConstantTime`, and `Password` all accept a `Secret` as
input directly.

## NonCryptoHash

```ts
NonCryptoHash.xxHash64(data): string;     // 16-char hex
```

Fast non-cryptographic hash for cache keys and integrity checks. **Not** for
security. The compiler emits a doctor warning
(`stdlib.crypto.noncrypto_hash.security_context`) when `xxHash64` appears
near security-looking identifiers, to nudge you toward `Hash` or `Hmac`.

## Examples

Hash and HMAC with a managed secret:

```ts
import { Hash, Hmac, Secret } from "sloppy/crypto";

const key = Secret.fromUtf8(signingKey);
try {
    const digest = await Hash.sha256Hex(payload);
    const signature = await Hmac.sha512(key, payload);
    const ok = await Hmac.verifySha512(key, payload, signature);
} finally {
    key.dispose();
}
```

Password hashing:

```ts
import { Password } from "sloppy/crypto";

const encoded = await Password.hash(password);
const matched = await Password.verify(password, encoded);
const upgrade = await Password.needsRehash(encoded);
```

Random tokens:

```ts
import { Random } from "sloppy/crypto";

const reset = Random.token(32);
const otp = Random.numericCode(6);
const id = Random.uuid();
```

In-repo references:

- `examples/crypto-random-token`
- `examples/crypto-hash-hmac`
- `examples/crypto-password`
- `examples/crypto-secret-constant-time`

## Boundaries

- No bring-your-own-algorithm. Hash and HMAC are SHA-256/384/512 only.
  Password is Argon2id only.
- No streaming HMAC or PBKDF2. Argon2id is the password KDF.
- No public AEAD/cipher API in `sloppy/crypto` today. Symmetric and asymmetric
  encryption are roadmap work.
- Node `crypto` compatibility is experimental and partial. It lives in the explicit
  `node:crypto` shim. It exposes `randomBytes`, `randomUUID`, SHA-2
  `createHash`, SHA-256 `createHmac`, and `timingSafeEqual` through this
  Sloppy crypto surface. It does not provide `crypto.subtle`, ciphers,
  PBKDF2, broad OpenSSL algorithm names, or full synchronous Node semantics.
- Secrets clone on construction and on `bytes()` — callers cannot mutate the
  underlying buffer through a returned view.

## Compiler source-input support

The compiler accepts `import { Random, Hash, Hmac, Password, ConstantTime, Secret, NonCryptoHash } from "sloppy/crypto"`
and emits the `stdlib.crypto` required feature. Other names from
`sloppy/crypto` are rejected. Aliased and default imports are rejected.

## Runtime requirements

`sloppy/crypto` requires the `__sloppy.crypto` V8 intrinsic namespace, which
the native runtime registers when the Plan declares `stdlib.crypto`. Random
sources, hashing, HMAC, constant-time equality, xxHash64, and Argon2id are
all platform-delegated. A native-only build that omits the crypto feature
will fail with `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` on the first call.

## Errors

| Code | Trigger |
| --- | --- |
| `SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` | bridge not installed |
| `SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM` | unknown algorithm or hasher reused |
| `SLOPPY_E_CRYPTO_SECRET_DISPOSED` | `Secret.bytes()` after `dispose()` |
| `SLOPPY_E_CRYPTO_INVALID_KEY_SECRET` | bounds or shape failure on key/value/signature |
| `SLOPPY_E_CRYPTO_PASSWORD_HASH_UNSUPPORTED` | `verify` / `needsRehash` against unrecognized format |
| `SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE` | platform Argon2 backend unavailable |
| `SLOPPY_E_CRYPTO_RANDOM_SOURCE_UNAVAILABLE` | OS CSPRNG unavailable |
| `SLOPPY_E_CRYPTO_CONSTANT_TIME_INVALID_INPUT` | `ConstantTime.equals` argument shape failure |

Argument-shape errors (non-string, non-Uint8Array, options out of range) are
plain `TypeError`.
