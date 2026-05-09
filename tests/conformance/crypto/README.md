# Crypto Conformance

This directory is the validation index.
Crypto conformance is split by lane:

- default native vectors: `core.crypto` covers OS-random API shape, UUID v4 text shape,
  token/hex/numeric alphabets, SHA-256/SHA-384/SHA-512 vectors, HMAC-SHA-256 vectors,
  constant-time equality correctness, Secret cleanup-once behavior, password
  hash/verify/needsRehash for the selected Argon2id PHC backend, unsupported password
  formats, and xxHash64 known-answer vectors;
- default diagnostics: `core.diagnostics.foundation` pins stable JSON goldens for crypto
  feature, algorithm, legacy/insecure warning, invalid key/secret, password verification
  failure, unsupported password hash, random source unavailable, disposed Secret,
  constant-time invalid input, backend unavailable, and `NonCryptoHash` security-context
  warning shapes;
- default source examples: `examples.crypto.api_shape` checks the public source examples
  for Random, Hash, Hmac, Password, ConstantTime, and Secret usage, and rejects obvious
  WebCrypto/Node/Bun, package-manager, benchmark, weak-random, secret-printing, or
  NonCryptoHash-in-security-example wording;
- bootstrap JavaScript stdlib coverage: `bootstrap.stdlib.app_host_foundation` executes
  the ESM stdlib with deterministic native-hook fakes and verifies the `Random`, `Hash`,
  `Hmac`, `Password`, `ConstantTime`, `Secret`, and `NonCryptoHash` public surface;
- compiler/tooling coverage: Rust compiler tests cover `sloppy/crypto` import activation
  for `stdlib.crypto` plus the bounded static doctor warning for visible
  `NonCryptoHash.xxHash64` use in security-looking contexts;
- V8-gated coverage: `conformance.v8.runtime_bridge` covers active `__sloppy.crypto`
  registration, SHA/HMAC/random/constant-time smoke, password offload owner-thread
  settlement, inactive-feature gating, and `NonCryptoHash` smoke when a V8 SDK is
  configured.

Randomness-quality analysis, password cracking-cost analysis, timing validation,
security validation for non-cryptographic hashes, release docs,
WebCrypto/Node/Bun compatibility, package-manager behavior, custom crypto
algorithms, weak-random fallback behavior, benchmark reports, and unrelated
filesystem/network/process behavior are separate work.
