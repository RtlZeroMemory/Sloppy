import assert from "node:assert/strict";

import { Base64, Base64Url, Binary, Hex, Text } from "../../stdlib/sloppy/index.js";

function assertBytes(actual, expected) {
    assert.deepEqual(Array.from(actual), Array.from(expected));
}

function makePrng(seed) {
    let state = seed >>> 0;
    return () => {
        state ^= state << 13;
        state ^= state >>> 17;
        state ^= state << 5;
        return state >>> 0;
    };
}

function makeBytes(length, seed) {
    const next = makePrng(seed);
    const bytes = new Uint8Array(length);
    for (let index = 0; index < bytes.length; index += 1) {
        bytes[index] = next() & 0xff;
    }
    return bytes;
}

function makeString(length, seed) {
    const next = makePrng(seed);
    let output = "";
    for (let index = 0; index < length; index += 1) {
        const value = next() % 5;
        if (value === 0) {
            output += "\0";
        } else if (value === 1) {
            output += String.fromCodePoint(0x20 + (next() % 0x5f));
        } else if (value === 2) {
            output += String.fromCodePoint(0x80 + (next() % 0x700));
        } else if (value === 3) {
            output += String.fromCodePoint(0x800 + (next() % 0x7000));
        } else {
            output += String.fromCodePoint(0x10000 + (next() % 0x1000));
        }
    }
    return output;
}

for (let length = 0; length <= 257; length += 1) {
    const bytes = makeBytes(length, 0xc0ffee ^ length);
    assertBytes(Base64.decode(Base64.encode(bytes)), bytes);
    assertBytes(Base64Url.decode(Base64Url.encode(bytes)), bytes);
    assertBytes(Base64Url.decode(Base64Url.encode(bytes, { padding: true }), { padding: "optional" }), bytes);
    assertBytes(Hex.decode(Hex.encode(bytes)), bytes);
}

for (let length = 0; length < 96; length += 1) {
    const text = makeString(length, 0x5eed1234 ^ length);
    assert.equal(Text.utf8.decode(Text.utf8.encode(text), { fatal: true }), text);
}

for (const bytes of [
    new Uint8Array([0]),
    new Uint8Array([0, 65, 0, 255]),
    makeBytes(64, 0x12345678),
]) {
    const writer = Binary.writer({ initialCapacity: 1, maxCapacity: 4096 });
    writer.u32le(bytes.length).bytes(bytes).u16be(0xabcd).i16le(-1234).u64be(0x0102030405060708n);
    const reader = Binary.reader(writer.toBytes());
    assert.equal(reader.u32le(), bytes.length);
    assertBytes(reader.bytes(bytes.length), bytes);
    assert.equal(reader.u16be(), 0xabcd);
    assert.equal(reader.i16le(), -1234);
    assert.equal(reader.u64be(), 0x0102030405060708n);
    assert.equal(reader.remaining(), 0);
}

for (const invalid of ["A", "====", "Zm9v-", "Z=g="]) {
    assert.throws(() => Base64.decode(invalid));
}

for (const invalid of ["-_8=", "Zm9v+", "A"]) {
    assert.throws(() => Base64Url.decode(invalid, { padding: "forbidden" }));
}

for (const invalid of ["f", "xx", "00xz"]) {
    assert.throws(() => Hex.decode(invalid));
}

for (const invalid of [
    new Uint8Array([0xc0, 0xaf]),
    new Uint8Array([0xe2, 0x82]),
    new Uint8Array([0xf8, 0x88, 0x80, 0x80, 0x80]),
]) {
    assert.throws(() => Text.utf8.decode(invalid, { fatal: true }));
}
