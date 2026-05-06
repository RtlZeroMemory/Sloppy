# Codec API Architecture

Status: CORE-CODEC-01.F/G implementation slice. CORE-CODEC-01.A/B defined the
`sloppy/codec` surface, backend/dependency policy, feature metadata, diagnostics, and
safety model; CORE-CODEC-01.C/D/I implements Base64, Base64Url, Hex, UTF-8 encode/decode,
the streaming UTF-8 decoder, bootstrap exports, and the feature-gated V8 namespace marker.
CORE-CODEC-01.E implements Binary reader/writer. CORE-CODEC-01.F/G implements zlib-backed
gzip/gunzip and bounded async-iterable compression transforms. Checksums, examples, and
final conformance goldens remain deferred.

## Goals

`sloppy/codec` gives Sloppy apps a compact runtime-owned API for non-cryptographic data
transforms:

- Base64, Base64Url, and hex encoding/decoding;
- UTF-8 text encoding/decoding and streaming UTF-8 decoder state;
- endian-explicit binary readers and writers;
- compression/decompression and later streaming compression transforms;
- checksums and non-security integrity conveniences;
- stable diagnostics and conformance vectors.

The module owns transformation boundaries only. It must not become Crypto, Network,
Filesystem, Process, HTTP, Workers, package-manager behavior, or compatibility with Node
`Buffer`, Web Streams, Bun, or Deno.

## Public Module

Applications import the API from:

```ts
import {
  Base64,
  Base64Url,
  Hex,
  Text,
  Binary,
  Compression,
  Checksums
} from "sloppy/codec";
```

The runtime feature descriptor is `stdlib.codec`. The compiler recognizes only named,
unaliased imports from `sloppy/codec`; the import adds `stdlib.codec` to Plan
`requiredFeatures[]`, emits `features.codec = true`, and emits
`strongPlan.evidence.codec = true`. The feature is available once V8 is available; the
private `__sloppy.codec` namespace is registered only for active `stdlib.codec` plans.

## API Contract

Encoding:

```ts
const encoded = Base64.encode(bytes);
const bytes = Base64.decode(encoded);

const token = Base64Url.encode(bytes, { padding: false });
const bytes2 = Base64Url.decode(token, { padding: "optional" });

const hex = Hex.encode(bytes);
const raw = Hex.decode(hex);
```

Text:

```ts
const textBytes = Text.utf8.encode("hello");
const text = Text.utf8.decode(textBytes, { fatal: true });

const decoder = Text.utf8.decoder({ fatal: true });
decoder.decode(chunk1, { stream: true });
decoder.decode(chunk2, { stream: true });
decoder.finish();
```

Binary:

```ts
const reader = Binary.reader(bytes);
const version = reader.u32le();
const length = reader.u16be();
const payload = reader.bytes(length);

const writer = Binary.writer();
writer.u32le(1);
writer.u16be(payload.length);
writer.bytes(payload);
const output = writer.toBytes();
```

Compression and checksums:

```ts
const gz = await Compression.gzip(bytes);
const raw2 = await Compression.gunzip(gz);
const stream = Compression.gzipStream(chunks, { signal, deadline });
```

Checksum examples land with CORE-CODEC-01.H/J, after the non-security checksum surface is
implemented.

## Encoding Policy

- Base64 uses the RFC 4648 standard alphabet with `=` padding by default.
- Base64Url is a distinct namespace using the URL-safe alphabet. It is not an alias for
  standard Base64.
- Base64Url encode supports `{ padding: true | false }`; default is `false` (unpadded).
- Base64Url decode supports `{ padding: "required" | "optional" | "forbidden" }`; default
  is `"optional"`.
- Decoders reject malformed alphabets, invalid padding placement, impossible lengths, and
  non-canonical trailing bits deterministically.
- Hex encodes lowercase by default. Decode accepts uppercase and lowercase hex, rejects odd
  digit counts, and rejects non-hex characters.
- All byte APIs preserve arbitrary bytes and embedded NUL values.

## Text Policy

- UTF-8 is the only text encoding selected for CORE-CODEC-01.
- Legacy encodings, Unicode table generation, locale-sensitive decoding, and BOM sniffing
  are deferred/rejected for this EPIC.
- `Text.utf8.decode(bytes, { fatal: true })` rejects malformed input.
- Replacement mode is explicit: fatal `false` replaces malformed sequences with U+FFFD.
- Streaming decoder state carries incomplete byte sequences across chunks.
- `finish()` rejects or replaces an incomplete trailing sequence according to fatal mode.
- UTF-8 BOM handling is explicit: a leading BOM is preserved by default; a later
  implementation may add `ignoreBOM` only if docs and vectors are updated together.

## Binary Policy

- `Binary.reader(bytes)` is bounds-checked and advances only after a successful read.
- Endian is method-explicit: `u16le`, `u16be`, `u32le`, `u32be`, and matching signed forms.
- `u8`, `i8`, `u16`, `i16`, `u32`, and `i32` return JavaScript `number`.
- `u64` and `i64` return JavaScript `bigint`; writers accept `bigint` for those widths
  and fail on values outside `0..=2^64-1` or `-(2^63)..=(2^63-1)` instead of wrapping or
  truncating. Signed reads sign-extend into the documented `bigint` range, and endian
  suffixes control byte order only.
- Float32/Float64 are selected only if a future IEEE-754 validation PR adds deterministic
  cross-platform vectors.
- Reader position, seek, and remaining are explicit. Negative seeks and overflow fail.
- Writer growth is bounded by an explicit `maxCapacity` option and a 64 MiB runtime
  maximum. Growth uses checked arithmetic and fails with stable diagnostics before
  allocatable buffer limits are reached.

## Compression Policy

- No custom compression algorithms.
- Gzip/gunzip uses the vetted `zlib` backend selected for CORE-CODEC-01.F/G.
- Deflate/inflate remains deferred until the same backend API is documented and tested
  without expanding the first gzip/gunzip surface.
- Brotli and zstd are deferred until a dependency is selected and default gates can report
  unavailable backends honestly.
- The native V8 gzip/gunzip bridge is capped to 1 MiB inline input; larger inputs fail
  before entering zlib. Any future larger or incremental native backend must offload work
  away from the V8 owner thread and settle Promises on that owner thread.
- Decompression has an explicit maximum output policy to protect against decompression
  bombs. `Compression.gunzip(bytes, { maxOutputBytes })` defaults to 64 MiB and exceeding
  the configured limit fails before reporting partial success.
- `Compression.gzip(bytes, { level })` accepts integer levels `0..9` and defaults to `6`.
- Streaming compression uses a Slop async-iterable Transform-style API, not a Web Streams
  compatibility promise. The current implementation accepts sync or async iterables of
  `Uint8Array` chunks, copies them into a bounded input buffer, invokes the same native
  gzip/gunzip backend once, and yields a single owned output chunk when the consumer pulls.
  It checks `signal` and `deadline` before chunk reads, before backend invocation, and
  after backend completion; cleanup is idempotent and no native handles are exposed.

## Checksums

- `Checksums.crc32(bytes)` is the selected initial checksum.
- CRC32C and Adler-32 are deferred until vectors and use cases are scoped.
- Checksums are non-security utilities only. They must not be presented as authentication,
  HMAC, signatures, password hashing, tokens, or attacker-resistant integrity.
- Security-looking checksum use may produce a compiler warning where statically visible.
- Cryptographic hashes and HMAC remain owned by `sloppy/crypto`.

## Feature And Plan Model

Feature id: `stdlib.codec`.

Public import: `sloppy/codec`.

Private V8 intrinsic namespace: `__sloppy.codec`.

Dependencies: `core`, `v8`, and `zlib`. The Base64/Base64Url/Hex/UTF-8 and Binary
implementations are bootstrap stdlib helpers. The private `__sloppy.codec` namespace is
registered only for active Plan-driven V8 execution and exposes bounded native gzip/gunzip
helpers under `src/engine/v8/*`.

Compiler behavior:

- named unaliased imports add `stdlib.codec` to Plan `requiredFeatures[]`;
- the compiler emits `features.codec = true`;
- the strong Plan evidence block emits `strongPlan.evidence.codec = true`;
- future PRs may record statically visible compression/checksum use and checksum
  security-context warnings.

Runtime behavior through CORE-CODEC-01.F/G:

- `stdlib.codec` is known to the feature registry;
- default availability is true when V8 is available;
- inactive or explicitly unavailable `stdlib.codec` still fails closed through
  runtime-feature diagnostics;
- `Base64`, `Base64Url`, `Hex`, `Text.utf8`, and `Binary` are implemented in
  `stdlib/sloppy/codec.js` and staged into the classic generated-app runtime;
- `Compression.gzip`, `Compression.gunzip`, `Compression.gzipStream`, and
  `Compression.gunzipStream` are implemented in the stdlib surface and call the active
  `__sloppy.codec` V8 bridge for gzip/gunzip bytes;
- `Checksums` exposes deterministic deferred stubs until CORE-CODEC-01.H/J lands.

## Diagnostics

Stable Codec diagnostics:

- `SLOPPY_E_CODEC_FEATURE_UNAVAILABLE`
- `SLOPPY_E_CODEC_UNSUPPORTED_ENCODING`
- `SLOPPY_E_CODEC_INVALID_BASE64`
- `SLOPPY_E_CODEC_INVALID_BASE64URL`
- `SLOPPY_E_CODEC_INVALID_HEX`
- `SLOPPY_E_CODEC_MALFORMED_UTF8`
- `SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS`
- `SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE`
- `SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE`
- `SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED`
- `SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT`
- `SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM`
- `SLOPPY_W_CODEC_CHECKSUM_SECURITY_CONTEXT_WARNING`

Codec diagnostics may name operation, encoding, algorithm id, backend family, byte length,
limit, and option shape. They must not include raw tokens, secret-like values, native
pointers, V8 handles, OS handles, or package-manager state.

## Evidence Boundaries

Default tests now prove the RFC 4648 Base64/Base64Url vectors, Hex vectors, arbitrary-byte
roundtrips, UTF-8 fatal/replacement behavior, streaming partial-sequence handling, BOM
preservation, Binary reader/writer endian and bounds behavior, stdlib export shape, the JS
compression option/backpressure/cancellation/deadline surface, and deterministic Checksum
deferred stubs. V8-gated tests prove active/inactive `__sloppy.codec` registration plus
real zlib-backed gzip/gunzip roundtrips, corrupt input, and decompression limit
diagnostics. They do not prove Checksums, performance, package readiness, public streaming
API compatibility, brotli/zstd/deflate support, or final conformance coverage.
Evidence lanes remain separate: default, V8-gated, package, dependency-backed,
streaming/stress, and benchmark.

## Non-Goals

- No Node `Buffer` compatibility promise.
- No Web Streams compatibility promise.
- No Bun/Deno compatibility promise.
- No custom compression algorithms.
- No arbitrary legacy text encodings.
- No cryptographic hash, HMAC, password, or secret API.
- No Network/Filesystem/Process/HTTP/Workers integration.
- No package-manager behavior.
- No public alpha docs.
- No benchmark or performance claims.
