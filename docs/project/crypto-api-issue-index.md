# CORE-CRYPTO-01 Issue Index

Parent EPIC: #571 CORE-CRYPTO-01: Crypto, Random, Hashing, Password, and Secret Utilities.

Status: CORE-CRYPTO-01.A/B/C/D/E/F/G/H/I source-of-truth index.

| Issue | Slice | PR Group | Status |
| --- | --- | --- | --- |
| #572 | API Contract and Backend Policy | PR 1 | Contract source docs, backend policy, and API surface locked. |
| #573 | Feature, Plan, Diagnostics, and Secret Redaction Model | PR 1 | `stdlib.crypto` feature, compiler Plan activation, diagnostics, and redaction model defined. |
| #574 | OS Random, UUID, Token, and Entropy Helpers | PR 2 | Implemented with OS CSPRNG backends, UUID v4, hex, URL-safe token, and numeric-code helpers. |
| #575 | Hash and HMAC APIs | PR 2 | Implemented SHA-256/SHA-384/SHA-512 and HMAC-SHA-256/SHA-384/SHA-512 through vetted backends. |
| #576 | Password Hashing and Verification | PR 3 | Implemented with libsodium Argon2id PHC strings and V8 owner-thread settlement. |
| #577 | Constant-Time and Secret Buffer Utilities | PR 2 | Implemented constant-time equality helper and cleanup-once Secret utilities. |
| #578 | Non-Cryptographic Hash Utilities | PR 4 | Implemented dependency-backed `NonCryptoHash.xxHash64` with security-separation warnings. |
| #579 | V8/Stdlib Integration and JS Surface | PR 2 | Implemented `__sloppy.crypto`, `stdlib/sloppy/crypto.js`, bootstrap runtime exports, and V8 smoke coverage. |
| #580 | Conformance, Test Vectors, Examples, Docs, and Goldens | PR 5 | Final source examples, conformance index, and evidence documentation. |

## PR Order

1. CORE-CRYPTO-01.A/B: contract, backend policy, feature id, Plan metadata, diagnostics,
   redaction policy.
2. CORE-CRYPTO-01.C/D/F/H: OS random, SHA-2, HMAC, constant-time compare, Secret, and
   V8/stdlib surface.
3. CORE-CRYPTO-01.E: password hashing, verification, needsRehash, and owner-thread offload.
4. CORE-CRYPTO-01.G: `NonCryptoHash` with explicit non-security separation.
5. CORE-CRYPTO-01.I: conformance, vectors, examples, docs, and goldens.

## Decisions Locked In PR 1

- Public import is `sloppy/crypto`.
- Runtime feature id is `stdlib.crypto`.
- Private V8 intrinsic namespace is `__sloppy.crypto`.
- Default runtime availability remains false until backends land.
- Secure random uses OS CSPRNG only.
- SHA-2/HMAC use OS crypto where suitable or vetted dependency backends.
- Password default is Argon2id PHC through `libsodium` and the V8 offload path.
- `bcrypt` and `PBKDF2` are compatibility/deferred, not defaults.
- `NonCryptoHash` is visibly separate from secure `Hash`/`Hmac`.
- `xxHash64` is the selected first non-crypto hash target.
- No WebCrypto, Node, Bun, package-manager, public alpha, benchmark, or custom algorithm
  claims are made by this EPIC.

## Implementation Decisions In PR 2

- Windows uses CNG for OS random, SHA-2, and HMAC.
- Linux uses `getrandom` for random; macOS uses `SecRandomCopyBytes`.
- Non-Windows SHA-2/HMAC uses OpenSSL 3 through the vetted dependency toolchain.
- Token and numeric-code helpers use rejection sampling; tests verify API shape and
  alphabet/UUID constraints only.
- V8 hash/HMAC inputs are capped at 1 MiB for the inline bridge.

## Implementation Decisions In PR 3

- Password hashing, verification, and needsRehash use libsodium Argon2id PHC strings.
- V8 password operations use worker-thread requests and settle promises on the V8 owner
  thread.

## Implementation Decisions In PR 4

- `NonCryptoHash.xxHash64(data)` uses the vetted `xxhash` dependency with seed `0`.
- The JS API returns a lowercase 16-character hex string to avoid 64-bit number precision
  traps.
- Compiler-emitted doctor checks warn when static source visibility suggests
  `NonCryptoHash.xxHash64` is being used in a security-looking context. The warning is a
  bounded static cue, not comprehensive enforcement.

## Final Evidence

CORE-CRYPTO-01.I adds `examples.crypto.api_shape`, `tests/conformance/crypto/README.md`,
four public Crypto source examples, and a consolidated evidence map across native vectors,
diagnostic goldens, compiler/doctor checks, bootstrap stdlib tests, and V8-gated bridge
smoke when the SDK is configured. Randomness quality, password cracking cost, timing-proof
behavior, security from non-cryptographic hashes, WebCrypto/Node/Bun compatibility,
package-manager behavior, public alpha readiness, and performance claims remain explicitly
out of scope for deterministic tests.

## Parent Completion Rule

The parent EPIC can close only after #572-#580 are closed and the final PR records evidence
for default gates, V8-gated Crypto tests where available, source examples, diagnostic
goldens, the selected backend/dependency decisions, and the explicit non-goals above.
