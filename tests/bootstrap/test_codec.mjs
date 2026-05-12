import assert from "node:assert/strict";

import { Base64, Base64Url, Binary, Checksums, Compression, Hex, Text } from "../../stdlib/sloppy/index.js";

const ascii = (value) => new Uint8Array(Array.from(value).map((char) => char.charCodeAt(0)));
const decodeChunks = (chunks) => chunks.map((chunk) => Text.utf8.decode(chunk)).join("");

function assertBytes(actual, expected) {
    assert.deepEqual(Array.from(actual), Array.from(expected));
}

function assertCodecError(fn, code) {
    assert.throws(fn, (error) => {
        assert.equal(error.code, code);
        assert.match(error.message, new RegExp(code));
        return true;
    });
}

function assertCodecErrorMessage(fn, code, messagePattern) {
    assert.throws(fn, (error) => {
        assert.equal(error.code, code);
        assert.match(error.message, new RegExp(code));
        assert.match(error.message, messagePattern);
        return true;
    });
}

const base64Vectors = [
    ["", ""],
    ["f", "Zg=="],
    ["fo", "Zm8="],
    ["foo", "Zm9v"],
    ["foob", "Zm9vYg=="],
    ["fooba", "Zm9vYmE="],
    ["foobar", "Zm9vYmFy"],
];

for (const [plain, encoded] of base64Vectors) {
    assert.equal(Base64.encode(ascii(plain)), encoded);
    assertBytes(Base64.decode(encoded), ascii(plain));
}

assert.equal(Base64Url.encode(new Uint8Array([0xfb, 0xff])), "-_8");
assert.equal(Base64Url.encode(new Uint8Array([0xfb, 0xff]), { padding: true }), "-_8=");
assertBytes(Base64Url.decode("-_8"), new Uint8Array([0xfb, 0xff]));
assertBytes(Base64Url.decode("-_8=", { padding: "optional" }), new Uint8Array([0xfb, 0xff]));
assertBytes(Base64Url.decode("-_8=", { padding: "required" }), new Uint8Array([0xfb, 0xff]));
assertCodecError(() => Base64Url.decode("-_8", { padding: "required" }), "SLOPPY_E_CODEC_INVALID_BASE64URL");
assertCodecError(() => Base64Url.decode("-_8=", { padding: "forbidden" }), "SLOPPY_E_CODEC_INVALID_BASE64URL");

assertCodecError(() => Base64.decode("A"), "SLOPPY_E_CODEC_INVALID_BASE64");
assertCodecError(() => Base64.decode("Zm9v-"), "SLOPPY_E_CODEC_INVALID_BASE64");
assertCodecError(() => Base64.decode("Z=g="), "SLOPPY_E_CODEC_INVALID_BASE64");
assertCodecError(() => Base64.decode("Zh=="), "SLOPPY_E_CODEC_INVALID_BASE64");
assertCodecError(() => Base64Url.decode("Zm9v+"), "SLOPPY_E_CODEC_INVALID_BASE64URL");

const arbitraryBytes = new Uint8Array([0x00, 0x0f, 0x10, 0xff]);
assert.equal(Hex.encode(arbitraryBytes), "000f10ff");
assertBytes(Hex.decode("000F10FF"), arbitraryBytes);
assertCodecError(() => Hex.decode("f"), "SLOPPY_E_CODEC_INVALID_HEX");
assertCodecError(() => Hex.decode("00xz"), "SLOPPY_E_CODEC_INVALID_HEX");

const text = "hello\0\uFEFF\u20AC\u{1F600}";
const textBytes = Text.utf8.encode(text);
assertBytes(textBytes, new Uint8Array([104, 101, 108, 108, 111, 0, 239, 187, 191, 226, 130, 172, 240, 159, 152, 128]));
assert.equal(Text.utf8.decode(textBytes, { fatal: true }), text);
assert.equal(Text.utf8.decode(new Uint8Array([0xef, 0xbb, 0xbf, 0x41])), "\uFEFFA");
assert.equal(Text.utf8.decode(new Uint8Array([0xc0, 0xaf])), "\uFFFD\uFFFD");
assert.equal(Text.utf8.decode(new Uint8Array([0xe0, 0x20])), "\uFFFD ");
assert.equal(Text.utf8.decode(new Uint8Array([0xe2, 0x82, 0x20])), "\uFFFD ");
assertCodecError(() => Text.utf8.decode(new Uint8Array([0xc0, 0xaf]), { fatal: true }), "SLOPPY_E_CODEC_MALFORMED_UTF8");

const streamingDecoder = Text.utf8.decoder({ fatal: true });
assert.equal(streamingDecoder.decode(new Uint8Array([0xe2, 0x82]), { stream: true }), "");
assert.equal(streamingDecoder.decode(new Uint8Array([0xac]), { stream: true }), "\u20AC");
assert.equal(streamingDecoder.finish(), "");

const replacementDecoder = Text.utf8.decoder();
assert.equal(replacementDecoder.decode(new Uint8Array([0xf0, 0x9f]), { stream: true }), "");
assert.equal(replacementDecoder.finish(), "\uFFFD");
const fatalDecoder = Text.utf8.decoder({ fatal: true });
assert.equal(fatalDecoder.decode(new Uint8Array([0xf0, 0x9f]), { stream: true }), "");
assertCodecError(() => fatalDecoder.finish(), "SLOPPY_E_CODEC_MALFORMED_UTF8");

assert.throws(() => Base64.encode("abc"), TypeError);
assert.throws(() => Base64Url.encode(new Uint8Array(0), { padding: "no" }), TypeError);
assert.throws(() => Base64Url.decode("", new Date()), TypeError);
assert.throws(() => Text.utf8.decode(new Uint8Array(0), { ignoreBOM: true }), TypeError);
assert.throws(() => Text.utf8.decoder(new Uint8Array(0)), TypeError);

assert.equal(Checksums.crc32(new Uint8Array(0)), 0);
assert.equal(Checksums.crc32(ascii("123456789")), 0xcbf43926);
assert.equal(Checksums.crc32(ascii("hello")), 0x3610a686);
assert.equal(Checksums.crc32(new Uint8Array([0, 1, 2, 3, 255])), 0x7b35f858);
assert.throws(() => Checksums.crc32("123456789"), TypeError);

const binaryReader = Binary.reader(new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8, 255, 254, 253, 252, 0]));
assert.equal(binaryReader.position(), 0);
assert.equal(binaryReader.remaining(), 13);
assert.equal(binaryReader.u32le(), 0x04030201);
assert.equal(binaryReader.u16be(), 0x0506);
assertBytes(binaryReader.bytes(2), new Uint8Array([7, 8]));
assert.equal(binaryReader.i8(), -1);
assert.equal(binaryReader.i16be(), -259);
assert.equal(binaryReader.remaining(), 2);
binaryReader.seek(0);
assert.equal(binaryReader.u16be(), 0x0102);
assert.equal(binaryReader.position(), 2);
binaryReader.seek(9);
assert.equal(binaryReader.i32be(), -16909312);
assert.equal(Binary.reader(new Uint8Array([8, 7, 6, 5, 4, 3, 2, 1])).u64le(), 0x0102030405060708n);
assert.equal(Binary.reader(new Uint8Array([0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe])).i64be(), -2n);

const payload = new Uint8Array([0, 65, 255]);
const binaryWriter = Binary.writer({ initialCapacity: 1, maxCapacity: 64 });
binaryWriter.u32le(1).u16be(payload.length).bytes(payload).i8(-1).i16le(-2).u64be(0x0102030405060708n).i64le(-2n);
assert.equal(binaryWriter.position(), 28);
const binaryWriterBytes = new Uint8Array([
    1, 0, 0, 0, 0, 3, 0, 65, 255, 255, 254, 255, 1, 2, 3, 4, 5, 6, 7, 8, 254, 255, 255, 255, 255, 255, 255, 255,
]);
assertBytes(binaryWriter.toBytes(), binaryWriterBytes);
const writtenReader = Binary.reader(binaryWriter.toBytes());
assert.equal(writtenReader.u32le(), 1);
assert.equal(writtenReader.u16be(), payload.length);
assertBytes(writtenReader.bytes(payload.length), payload);
assert.equal(writtenReader.i8(), -1);
assert.equal(writtenReader.i16le(), -2);
assert.equal(writtenReader.u64be(), 0x0102030405060708n);
assert.equal(writtenReader.i64le(), -2n);
assert.equal(writtenReader.remaining(), 0);

const writerSnapshot = binaryWriter.toBytes();
writerSnapshot[0] = 0xff;
assert.equal(Binary.reader(writerSnapshot).u32le(), 0xff);
assertBytes(binaryWriter.toBytes(), binaryWriterBytes);
assert.equal(Binary.reader(binaryWriter.toBytes()).u32le(), 1);

const shortReader = Binary.reader(new Uint8Array([1]));
assertCodecError(() => shortReader.u16le(), "SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS");
assert.equal(shortReader.position(), 0);
assertCodecError(() => shortReader.seek(2), "SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS");
assertCodecError(() => Binary.reader(new Uint8Array([1])).bytes(2), "SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS");
const cappedWriter = Binary.writer({ initialCapacity: 1, maxCapacity: 2 });
cappedWriter.u8(1).u8(2);
assertCodecErrorMessage(() => cappedWriter.u8(3), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE", /BinaryWriter\.u8/);
assertBytes(cappedWriter.toBytes(), new Uint8Array([1, 2]));
assertCodecError(() => Binary.writer({ initialCapacity: 3, maxCapacity: 2 }), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE");
assertCodecError(() => Binary.writer({ initialCapacity: 2 ** 40 }), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE");
assertCodecError(() => Binary.writer({ maxCapacity: 2 ** 40 }), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE");
assertCodecError(() => Binary.writer().u8(256), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE");
assertCodecError(() => Binary.writer().u64le(-1n), "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE");
assert.throws(() => Binary.reader([]), TypeError);

async function collectAsyncBytes(stream) {
    const chunks = [];
    let total = 0;
    for await (const chunk of stream) {
        total += chunk.byteLength;
        chunks.push(chunk);
    }
    const output = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        output.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return output;
}

async function collectAsyncChunks(stream) {
    const chunks = [];
    for await (const chunk of stream) {
        chunks.push(chunk);
    }
    return chunks;
}

function createAbortSignal() {
    const listeners = new Set();
    return {
        signal: {
            aborted: false,
            reason: undefined,
            addEventListener(type, listener) {
                if (type === "abort") {
                    listeners.add(listener);
                }
            },
            removeEventListener(type, listener) {
                if (type === "abort") {
                    listeners.delete(listener);
                }
            },
        },
        abort(reason) {
            this.signal.aborted = true;
            this.signal.reason = reason;
            for (const listener of listeners) {
                listener();
            }
        },
    };
}

function stalledAsyncIterable() {
    return {
        [Symbol.asyncIterator]() {
            return {
                next() {
                    return new Promise(() => {});
                },
                async return() {
                    return { done: true };
                },
            };
        },
    };
}

function sideEffectIterable() {
    return {
        reads: 0,
        [Symbol.iterator]() {
            const source = this;
            return {
                next() {
                    source.reads += 1;
                    return { done: true };
                },
            };
        },
    };
}

const previousSloppy = globalThis.__sloppy;
try {
    const fakeGzipPrefix = new Uint8Array([0x1f, 0x8b, 0x08, 0x00]);
    globalThis.__sloppy = {
        ...(previousSloppy ?? {}),
        codec: {
            gzip(bytes, level) {
                assert(level >= 0 && level <= 9);
                const output = new Uint8Array(fakeGzipPrefix.length + bytes.length);
                output.set(fakeGzipPrefix, 0);
                output.set(bytes, fakeGzipPrefix.length);
                return output;
            },
            gunzip(bytes, maxOutputBytes) {
                assert(maxOutputBytes >= 0);
                assertBytes(bytes.slice(0, fakeGzipPrefix.length), fakeGzipPrefix);
                if (bytes.length - fakeGzipPrefix.length > maxOutputBytes) {
                    throw new Error("SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED: fake output exceeded limit");
                }
                return bytes.slice(fakeGzipPrefix.length);
            },
        },
    };

    const compressed = await Compression.gzip(arbitraryBytes, { level: 9 });
    assertBytes(compressed.slice(0, 4), fakeGzipPrefix);
    assertBytes(await Compression.gunzip(compressed), arbitraryBytes);
    await assert.rejects(() => Compression.gunzip(compressed, { maxOutputBytes: 1 }), /SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED/);
    await assert.rejects(() => Compression.gzip(new Uint8Array(2 ** 20 + 1)), TypeError);
    await assert.rejects(() => Compression.gzip(arbitraryBytes, { level: 10 }), TypeError);
    await assert.rejects(() => Compression.gunzip(compressed, { maxOutputBytes: 2 ** 40 }), TypeError);

    const streamingCompressedChunks = await collectAsyncChunks(Compression.gzipStream([new Uint8Array([1]), new Uint8Array([2])]));
    assert.equal(streamingCompressedChunks.length, 1);
    const streamingCompressed = streamingCompressedChunks[0];
    assertBytes(await collectAsyncBytes(Compression.gunzipStream([streamingCompressed])), new Uint8Array([1, 2]));
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gzipStream([new Uint8Array([1]), new Uint8Array([2])], { maxInputBytes: 1 })),
        TypeError,
    );
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gzipStream([new Uint8Array([1])], { signal: { aborted: true, reason: "stop" } })),
        (error) => error.name === "CancelledError",
    );
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gunzipStream([compressed], { deadline: { remainingMs: () => 0 } })),
        (error) => error.name === "TimeoutError",
    );
    const invalidGzipOptionsInput = sideEffectIterable();
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gzipStream(invalidGzipOptionsInput, { level: 99 })),
        TypeError,
    );
    assert.equal(invalidGzipOptionsInput.reads, 0);
    const invalidGunzipOptionsInput = sideEffectIterable();
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gunzipStream(invalidGunzipOptionsInput, { maxOutputBytes: 2 ** 40 })),
        TypeError,
    );
    assert.equal(invalidGunzipOptionsInput.reads, 0);
    const controller = createAbortSignal();
    const stalledCompression = collectAsyncBytes(Compression.gzipStream(stalledAsyncIterable(), { signal: controller.signal }));
    controller.abort("stop");
    await assert.rejects(async () => stalledCompression, (error) => error.name === "CancelledError");
    await assert.rejects(
        async () => collectAsyncBytes(Compression.gzipStream(stalledAsyncIterable(), { deadline: { remainingMs: () => 1 } })),
        (error) => error.name === "TimeoutError",
    );
    assert.throws(() => Compression.gzipStream(null), TypeError);
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}

await assert.rejects(() => Compression.gzip(new Uint8Array(0)), /SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE/);

const previousRuntime = globalThis.__sloppy_runtime;
await import("../../stdlib/sloppy/internal/runtime-classic.js");
try {
    assert.equal(globalThis.__sloppy_runtime.Base64.encode(ascii("runtime")), "cnVudGltZQ==");
    assert.equal(globalThis.__sloppy_runtime.Text.utf8.decode(ascii("classic")), "classic");
    assert.equal(globalThis.__sloppy_runtime.Checksums.crc32(ascii("123456789")), 0xcbf43926);
    const runtimeAddress = globalThis.__sloppy_runtime.NetworkAddress.parse("[::1]:443");
    assert.equal(runtimeAddress.host, "::1");
    assert.equal(runtimeAddress.port, 443);
    assert.equal(String(runtimeAddress), "[::1]:443");
    assert.throws(
        () => globalThis.__sloppy_runtime.NetworkAddress.parse("::1:443"),
        /host:port/,
    );
    assert.throws(
        () => globalThis.__sloppy_runtime.NetworkAddress.parse("localhost:1e3"),
        /TCP port text/,
    );
    const runtimeWriter = globalThis.__sloppy_runtime.Binary.writer();
    runtimeWriter.u16le(0x1234).bytes(new Uint8Array([0]));
    assertBytes(runtimeWriter.toBytes(), new Uint8Array([0x34, 0x12, 0]));
    const runtimeSse = await globalThis.__sloppy_runtime.Realtime.sse(async (ctx, stream) => {
        stream.event("ready", { ok: true }, { id: "1", retry: 1000, comment: "connected" });
    })({});
    assert.equal(
        decodeChunks(runtimeSse.chunks),
        ": connected\nevent: ready\nid: 1\nretry: 1000\ndata: {\"ok\":true}\n\n",
    );
    const runtimeHub = globalThis.__sloppy_runtime.Realtime.hub("runtime");
    const runtimeFirst = runtimeHub.register();
    runtimeFirst.sendJson({ nested: { value: 1 } });
    const runtimeSnapshot = runtimeHub.__debug().connections[0].messages[0];
    assert.throws(() => {
        runtimeSnapshot.json.nested.value = 2;
    }, TypeError);
    runtimeFirst.close();
    const runtimeSecond = runtimeHub.register();
    assert.notEqual(runtimeFirst.id, runtimeSecond.id);
    assertCodecError(
        () => globalThis.__sloppy_runtime.Binary.writer({ initialCapacity: 2 ** 40 }),
        "SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE",
    );
} finally {
    if (previousRuntime === undefined) {
        delete globalThis.__sloppy_runtime;
    } else {
        globalThis.__sloppy_runtime = previousRuntime;
    }
}
