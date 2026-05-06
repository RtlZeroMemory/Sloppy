import assert from "node:assert/strict";

import { Base64, Base64Url, Binary, Checksums, Compression, Hex, Text } from "../../stdlib/sloppy/index.js";

const ascii = (value) => new Uint8Array(Array.from(value).map((char) => char.charCodeAt(0)));

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
assert.throws(() => Text.utf8.decode(new Uint8Array(0), { ignoreBOM: true }), TypeError);
assertCodecError(() => Binary.reader(new Uint8Array(0)), "SLOPPY_E_CODEC_FEATURE_UNAVAILABLE");
await assert.rejects(() => Compression.gzip(new Uint8Array(0)), /SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE/);
assertCodecError(() => Checksums.crc32(new Uint8Array(0)), "SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM");

const previousRuntime = globalThis.__sloppy_runtime;
await import("../../stdlib/sloppy/internal/runtime-classic.js");
try {
    assert.equal(globalThis.__sloppy_runtime.Base64.encode(ascii("runtime")), "cnVudGltZQ==");
    assert.equal(globalThis.__sloppy_runtime.Text.utf8.decode(ascii("classic")), "classic");
} finally {
    if (previousRuntime === undefined) {
        delete globalThis.__sloppy_runtime;
    } else {
        globalThis.__sloppy_runtime = previousRuntime;
    }
}
