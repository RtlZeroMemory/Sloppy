# Codec Conformance

This directory is the validation index.
Codec conformance is split by lane:

- default bootstrap JavaScript vectors: `bootstrap.stdlib.codec` covers RFC 4648
  Base64/Base64Url vectors, Hex vectors, arbitrary-byte and embedded-NUL roundtrips,
  UTF-8 fatal/replacement behavior, streaming UTF-8 partial sequences, Binary
  reader/writer endian and bounds behavior, gzip/gunzip option validation with native-hook
  fakes, async-iterable cancellation/deadline behavior, and CRC32 known-answer vectors;
- default compiler/tooling coverage: Rust compiler tests cover `sloppy/codec` import
  activation for `stdlib.codec`, generated runtime binding, and the static doctor warning
  for visible `Checksums.crc32` use in security-looking contexts;
- default examples: `examples.codec.api_shape` checks the Base64/Hex, Text/Binary,
  Compression, Streaming Compression, and Checksums examples, including example
  scope text for package, benchmark, and checksum-security boundaries;
- default diagnostics: `core.diagnostics.foundation` pins stable diagnostic code shapes for
  codec feature availability, malformed Base64/Base64Url/Hex/UTF-8, Binary bounds and
  field-size failures, compression backend/corruption/limit failures, and checksum
  security-context warning wording;
- V8-gated coverage: `engine.v8.smoke` and `conformance.v8.runtime_bridge` cover active and
  inactive `__sloppy.codec` registration plus zlib-backed gzip/gunzip roundtrip,
  decompression-limit, and corrupt-stream diagnostics when a V8 SDK is configured.

Node Buffer-style byte APIs, Web Streams-style APIs, custom compression
algorithms, brotli/zstd/deflate support, authentication or attacker-resistant
integrity, release docs, package-manager behavior, package release lanes, and
benchmark reports are separate work.
