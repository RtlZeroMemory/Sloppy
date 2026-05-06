# CORE-CRYPTO-01 Issue Index

Parent EPIC: #571 CORE-CRYPTO-01: Crypto, Random, Hashing, Password, and Secret Utilities.

Status: PR 1 contract and feature/diagnostic model.

| Issue | Slice | PR Group | Status |
| --- | --- | --- | --- |
| #572 | API Contract and Backend Policy | PR 1 | Contract source docs, backend policy, and API surface locked. |
| #573 | Feature, Plan, Diagnostics, and Secret Redaction Model | PR 1 | `stdlib.crypto` feature, compiler Plan activation, diagnostics, and redaction model defined. |
| #574 | OS Random, UUID, Token, and Entropy Helpers | PR 2 | Deferred until OS random backends and JS/V8 surface land. |
| #575 | Hash and HMAC APIs | PR 2 | Deferred until vetted backend integration and test vectors land. |
| #576 | Password Hashing and Verification | PR 3 | Deferred until Argon2id/offload implementation lands. |
| #577 | Constant-Time and Secret Buffer Utilities | PR 2 | Deferred until native/JS Secret and ConstantTime utilities land. |
| #578 | Non-Cryptographic Hash Utilities | PR 4 | Deferred until dependency-backed `NonCryptoHash` implementation lands. |
| #579 | V8/Stdlib Integration and JS Surface | PR 2 | Deferred until native intrinsics and bootstrap stdlib surface land. |
| #580 | Conformance, Test Vectors, Examples, Docs, and Goldens | PR 5 | Deferred until the final evidence/examples pass. |

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
- Password default target is Argon2id PHC through a vetted dependency and offload path.
- `bcrypt` and `PBKDF2` are compatibility/deferred, not defaults.
- `NonCryptoHash` is visibly separate from secure `Hash`/`Hmac`.
- `xxHash64` is the selected first non-crypto hash target.
- No WebCrypto, Node, Bun, package-manager, public alpha, benchmark, or custom algorithm
  claims are made by this EPIC.

## Evidence Expected Later

Future PRs must add standard hash/HMAC vectors, password format tests, random API shape and
failure diagnostics, secret redaction/disposal tests, non-crypto known-answer vectors,
V8-gated smoke where available, example source checks, and diagnostic goldens. Randomness
quality and performance claims are explicitly out of scope for deterministic tests.
