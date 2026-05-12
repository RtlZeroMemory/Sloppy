# Codec

`sloppy/codec` is the bootstrap stdlib encoding and binary helper surface. It
covers Base64, Base64URL, hex, UTF-8 text, fixed-endian binary I/O, gzip
compression, and CRC-32. Most of it is pure JS; only `Compression` needs the
runtime bridge.

## Import

```ts
import {
    Base64,
    Base64Url,
    Hex,
    Text,
    Binary,
    Compression,
    Checksums,
} from "sloppy/codec";
```

The compiler recognizes `sloppy/codec` as a stdlib subpath. Importing any of
these names emits the `stdlib.codec` runtime feature into the Plan.

## Current status

This public alpha, pre-production API shape is committed for current
experiments. All exports are frozen objects.
`Compression.gzip` / `gunzip` (and their stream variants) require the
`__sloppy.codec` bridge; without it they reject with
`SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE`. Everything else is pure JS.

## Base64 and Base64Url

```ts
Base64.encode(bytes: Uint8Array): string;
Base64.decode(text: string): Uint8Array;

Base64Url.encode(bytes: Uint8Array, options?: { padding?: boolean }): string;
Base64Url.decode(text: string, options?: { padding?: "optional" | "required" | "forbidden" }): Uint8Array;
```

`Base64` uses standard alphabet with required padding. `Base64Url` uses
`-_` alphabet and optional padding by default. Decoding rejects whitespace,
mixed alphabets, or non-canonical padding with
`SLOPPY_E_CODEC_INVALID_BASE64` / `SLOPPY_E_CODEC_INVALID_BASE64URL`.

## Hex

```ts
Hex.encode(bytes: Uint8Array): string;     // lowercase
Hex.decode(text: string): Uint8Array;      // case-insensitive
```

`Hex.decode` requires an even digit count. Bad input throws
`SLOPPY_E_CODEC_INVALID_HEX`.

## Text

`Text.utf8` is the only text encoder today.

```ts
Text.utf8.encode(text: string): Uint8Array;
Text.utf8.decode(bytes: Uint8Array, options?: { fatal?: boolean }): string;
Text.utf8.decoder(options?: { fatal?: boolean }): Utf8StreamingDecoder;
```

`decode` defaults to non-fatal mode (replaces malformed sequences with
`U+FFFD`). Pass `{ fatal: true }` to throw `SLOPPY_E_CODEC_MALFORMED_UTF8`.
The streaming decoder buffers partial sequences across `decode(chunk, { stream: true })`
calls and is flushed with `finish()`.

## Binary

`Binary.reader(bytes)` and `Binary.writer(options?)` give you fixed-endian
integer I/O over a `Uint8Array`.

Reader:

```ts
const reader = Binary.reader(bytes);
reader.u8(); reader.i8();
reader.u16le(); reader.u16be(); reader.i16le(); reader.i16be();
reader.u32le(); reader.u32be(); reader.i32le(); reader.i32be();
reader.u64le(); reader.u64be(); reader.i64le(); reader.i64be();   // bigint
reader.bytes(length);
reader.position(); reader.remaining(); reader.seek(position);
```

Writer:

```ts
const writer = Binary.writer({ initialCapacity: 64, maxCapacity: 64 * 1024 * 1024 });
writer.u32le(1).u16be(payload.length).bytes(payload);
const out = writer.toBytes();
```

Defaults: 64-byte initial capacity, 64 MiB max capacity. Writers expand
capacity automatically up to that ceiling. Out-of-range values or attempts
to read past the end throw `SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE`
or `SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS`.

`reader.bytes(length)` and `writer.toBytes()` always return a fresh
`Uint8Array` — the codec never hands out aliased views of internal buffers,
and `writer.bytes(input)` copies the input rather than retaining it.

## Compression

`Compression` is the one bridge-gated namespace. It exposes inline and
streaming gzip.

```ts
await Compression.gzip(bytes, { level?: number });            // 1..9, default 6
await Compression.gunzip(bytes, { maxOutputBytes?: number }); // default 64 MiB

for await (const chunk of Compression.gzipStream(input, options?)) { ... }
for await (const chunk of Compression.gunzipStream(input, options?)) { ... }
```

`input` for the streaming variants is an `Iterable<Uint8Array>` or
`AsyncIterable<Uint8Array>`. Stream options accept `signal`, `deadline`, and
`maxInputBytes` / `maxOutputBytes` size guards. The current implementation
buffers all input chunks up to `maxInputBytes`, invokes the whole-buffer gzip
or gunzip bridge, and yields one output chunk. It is not a Core stream adapter
and does not provide incremental zlib backpressure yet.

Errors:

| Code | Trigger |
| --- | --- |
| `SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE` | `__sloppy.codec` bridge missing |
| `SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED` | output exceeds `maxOutputBytes` |

## Checksums

```ts
Checksums.crc32(bytes: Uint8Array): number;     // CRC-32 (IEEE 802.3)
```

Pure JS, no bridge.

## Examples

Encoding round-trip:

```ts
import { Base64, Base64Url, Hex } from "sloppy/codec";

const bytes = new Uint8Array([0, 15, 16, 255]);
const b64 = Base64.encode(bytes);                          // "AA8Q/w=="
const b64u = Base64Url.encode(bytes, { padding: false });  // "AA8Q_w"
const hex = Hex.encode(bytes);                              // "000f10ff"
```

Binary record:

```ts
import { Binary, Text } from "sloppy/codec";

const payload = Text.utf8.encode("hello");
const writer = Binary.writer();
writer.u32le(1).u16be(payload.length).bytes(payload);
const packet = writer.toBytes();

const reader = Binary.reader(packet);
const version = reader.u32le();
const length = reader.u16be();
const text = Text.utf8.decode(reader.bytes(length), { fatal: true });
```

Compression round-trip:

```ts
import { Compression, Text } from "sloppy/codec";

const body = Text.utf8.encode("compress me");
const gz = await Compression.gzip(body, { level: 6 });
const raw = await Compression.gunzip(gz, { maxOutputBytes: 1024 * 1024 });
```

In-repo references:

- `examples/codec-base64-hex`
- `examples/codec-text-binary`
- `examples/codec-checksums`
- `examples/codec-compression`
- `examples/codec-streaming-compression`
- `examples/core-fs-time-codec`

## Boundaries

- No Node `Buffer`. Bytes are `Uint8Array`.
- No deflate, brotli, or zstd today — only gzip.
- No streaming Base64/hex. Encoders and decoders operate on whole values.
- Compression stream helpers are bounded whole-buffer adapters today.
- No `TextEncoder`/`TextDecoder` global; use `Text.utf8`.
- Decoders return fresh `Uint8Array` instances. Inputs are never retained.

## Compiler source-input support

The compiler accepts `import ... from "sloppy/codec"` and emits the
`stdlib.codec` required feature. Aliased and default imports of the module
are rejected by the compiler before the Plan is written.

## Runtime requirements

Pure-JS exports work in any host that runs Sloppy stdlib code. `Compression`
operations require the `__sloppy.codec` bridge installed by the native
runtime; absence of the bridge does not affect the other exports.

## Errors

`CodecError` is the structured error type. `error.code` carries one of:

- `SLOPPY_E_CODEC_INVALID_BASE64`
- `SLOPPY_E_CODEC_INVALID_BASE64URL`
- `SLOPPY_E_CODEC_INVALID_HEX`
- `SLOPPY_E_CODEC_MALFORMED_UTF8`
- `SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS`
- `SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE`
- `SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE`
- `SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED`

Argument-shape mistakes (non-string, non-Uint8Array, unknown options) throw
plain `TypeError`.
