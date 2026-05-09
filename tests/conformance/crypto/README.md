# Crypto Conformance

This directory is the evidence index.
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
- bootstrap JavaScript stdlib evidence: `bootstrap.stdlib.app_host_foundation` executes
  the ESM stdlib with deterministic native-hook fakes and verifies the `Random`, `Hash`,
  `Hmac`, `Password`, `ConstantTime`, `Secret`, and `NonCryptoHash` public surface;
- compiler/tooling evidence: Rust compiler tests cover `sloppy/crypto` import activation
  for `stdlib.crypto` plus the bounded static doctor warning for visible
  `NonCryptoHash.xxHash64` use in security-looking contexts;
- V8-gated evidence: `conformance.v8.runtime_bridge` covers active `__sloppy.crypto`
  registration, SHA/HMAC/random/constant-time smoke, password offload owner-thread
  settlement, inactive-feature gating, and `NonCryptoHash` smoke when a V8 SDK is
  configured.

This is not randomness-quality proof, password cracking-cost proof, timing-proof evidence,
security proof for non-cryptographic hashes, public release documentation, WebCrypto/Node/Bun
compatibility, package-manager behavior, custom crypto algorithm evidence, weak-random
fallback evidence, benchmark evidence, or unrelated filesystem/network/process behavior.
