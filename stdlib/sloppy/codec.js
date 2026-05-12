const BASE64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const BASE64URL_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
const HEX_ALPHABET = "0123456789abcdef";

class CodecError extends Error {
    constructor(code, message) {
        super(`${code}: ${message}`);
        this.name = "CodecError";
        this.code = code;
    }
}

function codecError(code, message) {
    return new CodecError(code, message);
}

function requireBytes(value, operation) {
    if (!(value instanceof Uint8Array)) {
        throw new TypeError(`${operation} requires Uint8Array bytes.`);
    }
    return value;
}

function requireString(value, operation) {
    if (typeof value !== "string") {
        throw new TypeError(`${operation} requires a string.`);
    }
    return value;
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function validateOptionsObject(options, operation) {
    if (options === undefined) {
        return {};
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be an object when provided.`);
    }
    return options;
}

function rejectUnknownOptions(options, allowed, operation) {
    for (const key of Object.keys(options)) {
        if (!allowed.has(key)) {
            throw new TypeError(`${operation} does not support option ${key}.`);
        }
    }
}

function encodeBase64(bytes, alphabet, padding) {
    requireBytes(bytes, "Base64.encode");
    let output = "";
    for (let offset = 0; offset < bytes.length; offset += 3) {
        const b0 = bytes[offset];
        const b1 = offset + 1 < bytes.length ? bytes[offset + 1] : 0;
        const b2 = offset + 2 < bytes.length ? bytes[offset + 2] : 0;
        const triple = (b0 << 16) | (b1 << 8) | b2;
        output += alphabet[(triple >>> 18) & 0x3f];
        output += alphabet[(triple >>> 12) & 0x3f];
        output += offset + 1 < bytes.length ? alphabet[(triple >>> 6) & 0x3f] : "=";
        output += offset + 2 < bytes.length ? alphabet[triple & 0x3f] : "=";
    }
    return padding ? output : output.replace(/=+$/u, "");
}

function makeAlphabetMap(alphabet) {
    const map = new Map();
    for (let index = 0; index < alphabet.length; index += 1) {
        map.set(alphabet[index], index);
    }
    return map;
}

const BASE64_MAP = makeAlphabetMap(BASE64_ALPHABET);
const BASE64URL_MAP = makeAlphabetMap(BASE64URL_ALPHABET);

function normalizeBase64Input(text, kind, paddingMode) {
    const code = kind === "base64url" ? "SLOPPY_E_CODEC_INVALID_BASE64URL" : "SLOPPY_E_CODEC_INVALID_BASE64";
    if (text.length === 0) {
        return text;
    }
    if (/\s/u.test(text)) {
        throw codecError(code, `${kind} input must not contain whitespace.`);
    }
    const firstPadding = text.indexOf("=");
    if (firstPadding !== -1 && !/^=+$/u.test(text.slice(firstPadding))) {
        throw codecError(code, `${kind} padding must appear only at the end.`);
    }
    if (kind === "base64url") {
        if (/[+/]/u.test(text)) {
            throw codecError(code, "Base64Url input must use the URL-safe alphabet.");
        }
        if (paddingMode === "forbidden" && firstPadding !== -1) {
            throw codecError(code, "Base64Url padding is forbidden for this decode.");
        }
        if (paddingMode === "required" && text.length % 4 !== 0) {
            throw codecError(code, "Base64Url padding is required for this decode.");
        }
    } else if (/[-_]/u.test(text)) {
        throw codecError(code, "Base64 input must use the standard alphabet.");
    }
    const paddingCount = firstPadding === -1 ? 0 : text.length - firstPadding;
    if (paddingCount > 2) {
        throw codecError(code, `${kind} input has invalid padding.`);
    }
    if (text.length % 4 === 1) {
        throw codecError(code, `${kind} input length is impossible.`);
    }
    if (text.length % 4 !== 0) {
        if (kind !== "base64url" || paddingMode === "required" || paddingCount !== 0) {
            throw codecError(code, `${kind} input length is invalid.`);
        }
        return text.padEnd(text.length + (4 - (text.length % 4)), "=");
    }
    return text;
}

function decodeBase64(text, alphabetMap, kind, paddingMode) {
    text = normalizeBase64Input(requireString(text, `${kind}.decode`), kind, paddingMode);
    const code = kind === "base64url" ? "SLOPPY_E_CODEC_INVALID_BASE64URL" : "SLOPPY_E_CODEC_INVALID_BASE64";
    const bytes = [];
    for (let offset = 0; offset < text.length; offset += 4) {
        const c0 = text[offset];
        const c1 = text[offset + 1];
        const c2 = text[offset + 2];
        const c3 = text[offset + 3];
        const p2 = c2 === "=";
        const p3 = c3 === "=";
        if (c0 === "=" || c1 === "=" || (p2 && !p3)) {
            throw codecError(code, `${kind} input has invalid padding placement.`);
        }
        const v0 = alphabetMap.get(c0);
        const v1 = alphabetMap.get(c1);
        const v2 = p2 ? 0 : alphabetMap.get(c2);
        const v3 = p3 ? 0 : alphabetMap.get(c3);
        if (v0 === undefined || v1 === undefined || v2 === undefined || v3 === undefined) {
            throw codecError(code, `${kind} input contains a non-alphabet character.`);
        }
        if (p2 && (v1 & 0x0f) !== 0) {
            throw codecError(code, `${kind} input has non-canonical trailing bits.`);
        }
        if (!p2 && p3 && (v2 & 0x03) !== 0) {
            throw codecError(code, `${kind} input has non-canonical trailing bits.`);
        }
        const triple = (v0 << 18) | (v1 << 12) | (v2 << 6) | v3;
        bytes.push((triple >>> 16) & 0xff);
        if (!p2) {
            bytes.push((triple >>> 8) & 0xff);
        }
        if (!p3) {
            bytes.push(triple & 0xff);
        }
    }
    return new Uint8Array(bytes);
}

function parseBase64UrlEncodeOptions(options) {
    options = validateOptionsObject(options, "Base64Url.encode");
    rejectUnknownOptions(options, new Set(["padding"]), "Base64Url.encode");
    if (options.padding === undefined) {
        return false;
    }
    if (typeof options.padding !== "boolean") {
        throw new TypeError("Base64Url.encode padding must be boolean.");
    }
    return options.padding;
}

function parseBase64UrlDecodeOptions(options) {
    options = validateOptionsObject(options, "Base64Url.decode");
    rejectUnknownOptions(options, new Set(["padding"]), "Base64Url.decode");
    const padding = options.padding ?? "optional";
    if (padding !== "optional" && padding !== "required" && padding !== "forbidden") {
        throw new TypeError('Base64Url.decode padding must be "optional", "required", or "forbidden".');
    }
    return padding;
}

function decodeHexNibble(code) {
    if (code >= 48 && code <= 57) {
        return code - 48;
    }
    if (code >= 65 && code <= 70) {
        return code - 55;
    }
    if (code >= 97 && code <= 102) {
        return code - 87;
    }
    return -1;
}

function encodeUtf8(text) {
    text = requireString(text, "Text.utf8.encode");
    const bytes = [];
    for (const char of text) {
        let codePoint = char.codePointAt(0);
        if (codePoint >= 0xd800 && codePoint <= 0xdfff) {
            codePoint = 0xfffd;
        }
        if (codePoint <= 0x7f) {
            bytes.push(codePoint);
        } else if (codePoint <= 0x7ff) {
            bytes.push(0xc0 | (codePoint >>> 6), 0x80 | (codePoint & 0x3f));
        } else if (codePoint <= 0xffff) {
            bytes.push(0xe0 | (codePoint >>> 12), 0x80 | ((codePoint >>> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
        } else {
            bytes.push(
                0xf0 | (codePoint >>> 18),
                0x80 | ((codePoint >>> 12) & 0x3f),
                0x80 | ((codePoint >>> 6) & 0x3f),
                0x80 | (codePoint & 0x3f),
            );
        }
    }
    return new Uint8Array(bytes);
}

function utf8Malformed(fatal, message) {
    if (fatal) {
        throw codecError("SLOPPY_E_CODEC_MALFORMED_UTF8", message);
    }
    return "\uFFFD";
}

function isContinuation(byte) {
    return byte >= 0x80 && byte <= 0xbf;
}

function decodeUtf8Bytes(bytes, options) {
    const fatal = options.fatal;
    const stream = options.stream;
    let output = "";
    let offset = 0;
    while (offset < bytes.length) {
        const b0 = bytes[offset];
        if (b0 <= 0x7f) {
            output += String.fromCodePoint(b0);
            offset += 1;
            continue;
        }
        let needed = 0;
        let codePoint = 0;
        let minSecond = 0x80;
        let maxSecond = 0xbf;
        if (b0 >= 0xc2 && b0 <= 0xdf) {
            needed = 2;
            codePoint = b0 & 0x1f;
        } else if (b0 >= 0xe0 && b0 <= 0xef) {
            needed = 3;
            codePoint = b0 & 0x0f;
            if (b0 === 0xe0) {
                minSecond = 0xa0;
            } else if (b0 === 0xed) {
                maxSecond = 0x9f;
            }
        } else if (b0 >= 0xf0 && b0 <= 0xf4) {
            needed = 4;
            codePoint = b0 & 0x07;
            if (b0 === 0xf0) {
                minSecond = 0x90;
            } else if (b0 === 0xf4) {
                maxSecond = 0x8f;
            }
        } else {
            output += utf8Malformed(fatal, "UTF-8 input contains an invalid leading byte.");
            offset += 1;
            continue;
        }
        if (offset + 1 >= bytes.length) {
            if (stream) {
                break;
            }
            output += utf8Malformed(fatal, "UTF-8 input ended with an incomplete sequence.");
            offset = bytes.length;
            break;
        }
        const b1 = bytes[offset + 1];
        if (b1 < minSecond || b1 > maxSecond) {
            output += utf8Malformed(fatal, "UTF-8 input contains an invalid continuation byte.");
            offset += 1;
            continue;
        }
        if (offset + needed > bytes.length) {
            if (stream) {
                break;
            }
            output += utf8Malformed(fatal, "UTF-8 input ended with an incomplete sequence.");
            offset = bytes.length;
            break;
        }
        codePoint = (codePoint << 6) | (b1 & 0x3f);
        let valid = true;
        let invalidIndex = 0;
        for (let index = 2; index < needed; index += 1) {
            const next = bytes[offset + index];
            if (!isContinuation(next)) {
                valid = false;
                invalidIndex = index;
                break;
            }
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (!valid) {
            output += utf8Malformed(fatal, "UTF-8 input contains an invalid continuation byte.");
            offset += invalidIndex;
            continue;
        }
        output += String.fromCodePoint(codePoint);
        offset += needed;
    }
    return { output, remaining: bytes.slice(offset) };
}

function parseUtf8Options(options, operation) {
    options = validateOptionsObject(options, operation);
    rejectUnknownOptions(options, new Set(["fatal"]), operation);
    if (options.fatal !== undefined && typeof options.fatal !== "boolean") {
        throw new TypeError(`${operation} fatal must be boolean.`);
    }
    return { fatal: options.fatal === true };
}

function parseDecodeChunkOptions(options) {
    options = validateOptionsObject(options, "Text.utf8.decoder.decode");
    rejectUnknownOptions(options, new Set(["stream"]), "Text.utf8.decoder.decode");
    if (options.stream !== undefined && typeof options.stream !== "boolean") {
        throw new TypeError("Text.utf8.decoder.decode stream must be boolean.");
    }
    return { stream: options.stream === true };
}

class Utf8StreamingDecoder {
    constructor(options = undefined) {
        this._fatal = parseUtf8Options(options, "Text.utf8.decoder").fatal;
        this._pending = new Uint8Array(0);
    }

    decode(chunk, options = undefined) {
        chunk = requireBytes(chunk, "Text.utf8.decoder.decode");
        const { stream } = parseDecodeChunkOptions(options);
        const input = new Uint8Array(this._pending.length + chunk.length);
        input.set(this._pending, 0);
        input.set(chunk, this._pending.length);
        const decoded = decodeUtf8Bytes(input, { fatal: this._fatal, stream });
        this._pending = decoded.remaining;
        return decoded.output;
    }

    finish() {
        const decoded = decodeUtf8Bytes(this._pending, { fatal: this._fatal, stream: false });
        this._pending = new Uint8Array(0);
        return decoded.output;
    }
}

const Base64 = Object.freeze({
    encode(bytes) {
        return encodeBase64(requireBytes(bytes, "Base64.encode"), BASE64_ALPHABET, true);
    },
    decode(text) {
        return decodeBase64(text, BASE64_MAP, "base64", "required");
    },
});

const Base64Url = Object.freeze({
    encode(bytes, options = undefined) {
        return encodeBase64(requireBytes(bytes, "Base64Url.encode"), BASE64URL_ALPHABET, parseBase64UrlEncodeOptions(options));
    },
    decode(text, options = undefined) {
        return decodeBase64(text, BASE64URL_MAP, "base64url", parseBase64UrlDecodeOptions(options));
    },
});

const Hex = Object.freeze({
    encode(bytes) {
        bytes = requireBytes(bytes, "Hex.encode");
        let output = "";
        for (const byte of bytes) {
            output += HEX_ALPHABET[byte >>> 4] + HEX_ALPHABET[byte & 0x0f];
        }
        return output;
    },
    decode(text) {
        text = requireString(text, "Hex.decode");
        if (text.length % 2 !== 0) {
            throw codecError("SLOPPY_E_CODEC_INVALID_HEX", "Hex input must have an even digit count.");
        }
        const bytes = new Uint8Array(text.length / 2);
        for (let offset = 0; offset < text.length; offset += 2) {
            const hi = decodeHexNibble(text.charCodeAt(offset));
            const lo = decodeHexNibble(text.charCodeAt(offset + 1));
            if (hi < 0 || lo < 0) {
                throw codecError("SLOPPY_E_CODEC_INVALID_HEX", "Hex input contains a non-hex digit.");
            }
            bytes[offset / 2] = (hi << 4) | lo;
        }
        return bytes;
    },
});

const Text = Object.freeze({
    utf8: Object.freeze({
        encode: encodeUtf8,
        decode(bytes, options = undefined) {
            bytes = requireBytes(bytes, "Text.utf8.decode");
            return decodeUtf8Bytes(bytes, { ...parseUtf8Options(options, "Text.utf8.decode"), stream: false }).output;
        },
        decoder(options = undefined) {
            return new Utf8StreamingDecoder(options);
        },
    }),
});

const DEFAULT_BINARY_WRITER_CAPACITY = 64;
const DEFAULT_BINARY_WRITER_MAX_CAPACITY = 64 * 1024 * 1024;
const UINT64_MAX = (1n << 64n) - 1n;
const INT64_MIN = -(1n << 63n);
const INT64_MAX = (1n << 63n) - 1n;

function binaryBoundsError(message) {
    return codecError("SLOPPY_E_CODEC_BINARY_READ_OUT_OF_BOUNDS", message);
}

function binaryFieldError(message) {
    return codecError("SLOPPY_E_CODEC_BINARY_INVALID_ENDIAN_OR_FIELD_SIZE", message);
}

function requireNonNegativeInteger(value, operation) {
    if (!Number.isSafeInteger(value) || value < 0) {
        throw binaryFieldError(`${operation} requires a non-negative safe integer.`);
    }
    return value;
}

function requireBinaryCapacity(value, operation) {
    value = requireNonNegativeInteger(value, operation);
    if (value > DEFAULT_BINARY_WRITER_MAX_CAPACITY) {
        throw binaryFieldError(`${operation} must not exceed the Binary.writer runtime maximum.`);
    }
    return value;
}

function requireIntegerInRange(value, min, max, operation) {
    if (!Number.isInteger(value) || value < min || value > max) {
        throw binaryFieldError(`${operation} value is outside the supported field range.`);
    }
    return value;
}

function requireBigIntInRange(value, min, max, operation) {
    if (typeof value !== "bigint" || value < min || value > max) {
        throw binaryFieldError(`${operation} value is outside the supported field range.`);
    }
    return value;
}

function readUnsignedNumber(bytes, offset, width, littleEndian) {
    let value = 0;
    for (let index = 0; index < width; index += 1) {
        const byte = littleEndian ? bytes[offset + index] : bytes[offset + width - 1 - index];
        value += byte * 2 ** (8 * index);
    }
    return value;
}

function readUnsignedBigInt(bytes, offset, width, littleEndian) {
    let value = 0n;
    if (littleEndian) {
        for (let index = width - 1; index >= 0; index -= 1) {
            value = (value << 8n) | BigInt(bytes[offset + index]);
        }
    } else {
        for (let index = 0; index < width; index += 1) {
            value = (value << 8n) | BigInt(bytes[offset + index]);
        }
    }
    return value;
}

function writeUnsignedNumber(bytes, offset, width, value, littleEndian) {
    for (let index = 0; index < width; index += 1) {
        const byte = Math.floor(value / 2 ** (8 * index)) & 0xff;
        bytes[offset + (littleEndian ? index : width - 1 - index)] = byte;
    }
}

function writeUnsignedBigInt(bytes, offset, width, value, littleEndian) {
    for (let index = 0; index < width; index += 1) {
        const byte = Number((value >> (8n * BigInt(index))) & 0xffn);
        bytes[offset + (littleEndian ? index : width - 1 - index)] = byte;
    }
}

class BinaryReader {
    #bytes;
    #offset = 0;

    constructor(bytes) {
        this.#bytes = new Uint8Array(requireBytes(bytes, "Binary.reader"));
    }

    position() {
        return this.#offset;
    }

    remaining() {
        return this.#bytes.length - this.#offset;
    }

    seek(position) {
        position = requireNonNegativeInteger(position, "BinaryReader.seek");
        if (position > this.#bytes.length) {
            throw binaryBoundsError("BinaryReader.seek moved beyond the input length.");
        }
        this.#offset = position;
        return this;
    }

    bytes(length) {
        length = requireNonNegativeInteger(length, "BinaryReader.bytes");
        const offset = this.#reserve(length, "BinaryReader.bytes");
        return this.#bytes.slice(offset, offset + length);
    }

    u8() {
        return this.#readNumber(1, false, false, "BinaryReader.u8");
    }

    i8() {
        return this.#readNumber(1, true, false, "BinaryReader.i8");
    }

    u16le() {
        return this.#readNumber(2, false, true, "BinaryReader.u16le");
    }

    u16be() {
        return this.#readNumber(2, false, false, "BinaryReader.u16be");
    }

    i16le() {
        return this.#readNumber(2, true, true, "BinaryReader.i16le");
    }

    i16be() {
        return this.#readNumber(2, true, false, "BinaryReader.i16be");
    }

    u32le() {
        return this.#readNumber(4, false, true, "BinaryReader.u32le");
    }

    u32be() {
        return this.#readNumber(4, false, false, "BinaryReader.u32be");
    }

    i32le() {
        return this.#readNumber(4, true, true, "BinaryReader.i32le");
    }

    i32be() {
        return this.#readNumber(4, true, false, "BinaryReader.i32be");
    }

    u64le() {
        return this.#readBigInt(false, true, "BinaryReader.u64le");
    }

    u64be() {
        return this.#readBigInt(false, false, "BinaryReader.u64be");
    }

    i64le() {
        return this.#readBigInt(true, true, "BinaryReader.i64le");
    }

    i64be() {
        return this.#readBigInt(true, false, "BinaryReader.i64be");
    }

    #reserve(length, operation) {
        if (length > this.remaining()) {
            throw binaryBoundsError(`${operation} requires ${length} byte(s), but only ${this.remaining()} remain.`);
        }
        const offset = this.#offset;
        this.#offset += length;
        return offset;
    }

    #readNumber(width, signed, littleEndian, operation) {
        const offset = this.#reserve(width, operation);
        const unsigned = readUnsignedNumber(this.#bytes, offset, width, littleEndian);
        if (!signed) {
            return unsigned;
        }
        const signBoundary = 2 ** (width * 8 - 1);
        const fullRange = 2 ** (width * 8);
        return unsigned >= signBoundary ? unsigned - fullRange : unsigned;
    }

    #readBigInt(signed, littleEndian, operation) {
        const offset = this.#reserve(8, operation);
        const unsigned = readUnsignedBigInt(this.#bytes, offset, 8, littleEndian);
        if (!signed) {
            return unsigned;
        }
        return unsigned > INT64_MAX ? unsigned - (1n << 64n) : unsigned;
    }
}

class BinaryWriter {
    #bytes;
    #length = 0;
    #maxCapacity;

    constructor(options = {}) {
        options = validateOptionsObject(options, "Binary.writer");
        rejectUnknownOptions(options, new Set(["initialCapacity", "maxCapacity"]), "Binary.writer");
        const initialCapacity = options.initialCapacity === undefined
            ? DEFAULT_BINARY_WRITER_CAPACITY
            : requireBinaryCapacity(options.initialCapacity, "Binary.writer initialCapacity");
        this.#maxCapacity = options.maxCapacity === undefined
            ? DEFAULT_BINARY_WRITER_MAX_CAPACITY
            : requireBinaryCapacity(options.maxCapacity, "Binary.writer maxCapacity");
        if (initialCapacity > this.#maxCapacity) {
            throw binaryFieldError("Binary.writer initialCapacity must not exceed maxCapacity.");
        }
        try {
            this.#bytes = new Uint8Array(initialCapacity);
        } catch {
            throw binaryFieldError("Binary.writer initialCapacity could not be allocated.");
        }
    }

    position() {
        return this.#length;
    }

    toBytes() {
        return this.#bytes.slice(0, this.#length);
    }

    bytes(bytes) {
        bytes = requireBytes(bytes, "BinaryWriter.bytes");
        const offset = this.#reserve(bytes.length, "BinaryWriter.bytes");
        this.#bytes.set(bytes, offset);
        return this;
    }

    u8(value) {
        return this.#writeNumber(1, requireIntegerInRange(value, 0, 0xff, "BinaryWriter.u8"), true, "BinaryWriter.u8");
    }

    i8(value) {
        return this.#writeNumber(1, requireIntegerInRange(value, -0x80, 0x7f, "BinaryWriter.i8"), true, "BinaryWriter.i8");
    }

    u16le(value) {
        return this.#writeNumber(2, requireIntegerInRange(value, 0, 0xffff, "BinaryWriter.u16le"), true, "BinaryWriter.u16le");
    }

    u16be(value) {
        return this.#writeNumber(2, requireIntegerInRange(value, 0, 0xffff, "BinaryWriter.u16be"), false, "BinaryWriter.u16be");
    }

    i16le(value) {
        return this.#writeNumber(2, requireIntegerInRange(value, -0x8000, 0x7fff, "BinaryWriter.i16le"), true, "BinaryWriter.i16le");
    }

    i16be(value) {
        return this.#writeNumber(2, requireIntegerInRange(value, -0x8000, 0x7fff, "BinaryWriter.i16be"), false, "BinaryWriter.i16be");
    }

    u32le(value) {
        return this.#writeNumber(4, requireIntegerInRange(value, 0, 0xffffffff, "BinaryWriter.u32le"), true, "BinaryWriter.u32le");
    }

    u32be(value) {
        return this.#writeNumber(4, requireIntegerInRange(value, 0, 0xffffffff, "BinaryWriter.u32be"), false, "BinaryWriter.u32be");
    }

    i32le(value) {
        return this.#writeNumber(4, requireIntegerInRange(value, -0x80000000, 0x7fffffff, "BinaryWriter.i32le"), true, "BinaryWriter.i32le");
    }

    i32be(value) {
        return this.#writeNumber(4, requireIntegerInRange(value, -0x80000000, 0x7fffffff, "BinaryWriter.i32be"), false, "BinaryWriter.i32be");
    }

    u64le(value) {
        return this.#writeBigInt(requireBigIntInRange(value, 0n, UINT64_MAX, "BinaryWriter.u64le"), true, "BinaryWriter.u64le");
    }

    u64be(value) {
        return this.#writeBigInt(requireBigIntInRange(value, 0n, UINT64_MAX, "BinaryWriter.u64be"), false, "BinaryWriter.u64be");
    }

    i64le(value) {
        value = requireBigIntInRange(value, INT64_MIN, INT64_MAX, "BinaryWriter.i64le");
        return this.#writeBigInt(value < 0n ? value + (1n << 64n) : value, true, "BinaryWriter.i64le");
    }

    i64be(value) {
        value = requireBigIntInRange(value, INT64_MIN, INT64_MAX, "BinaryWriter.i64be");
        return this.#writeBigInt(value < 0n ? value + (1n << 64n) : value, false, "BinaryWriter.i64be");
    }

    #reserve(length, operation) {
        if (length > this.#maxCapacity - this.#length) {
            throw binaryFieldError(`${operation} would exceed Binary.writer maxCapacity.`);
        }
        const offset = this.#length;
        const required = offset + length;
        this.#ensureCapacity(required, operation);
        this.#length = required;
        return offset;
    }

    #ensureCapacity(required, operation) {
        if (required <= this.#bytes.length) {
            return;
        }
        let next = Math.max(1, this.#bytes.length);
        while (next < required) {
            next *= 2;
            if (next > this.#maxCapacity) {
                next = this.#maxCapacity;
                break;
            }
        }
        let grown;
        try {
            grown = new Uint8Array(next);
        } catch {
            throw binaryFieldError(`${operation} could not grow Binary.writer capacity.`);
        }
        grown.set(this.#bytes.subarray(0, this.#length));
        this.#bytes = grown;
    }

    #writeNumber(width, value, littleEndian, operation) {
        const bits = width * 8;
        const unsigned = value < 0 ? value + 2 ** bits : value;
        const offset = this.#reserve(width, operation);
        writeUnsignedNumber(this.#bytes, offset, width, unsigned, littleEndian);
        return this;
    }

    #writeBigInt(value, littleEndian, operation) {
        const offset = this.#reserve(8, operation);
        writeUnsignedBigInt(this.#bytes, offset, 8, value, littleEndian);
        return this;
    }
}

const Binary = Object.freeze({
    reader(bytes) {
        return new BinaryReader(bytes);
    },
    writer(options) {
        return new BinaryWriter(options);
    },
});

const DEFAULT_COMPRESSION_LEVEL = 6;
const MAX_COMPRESSION_INPUT_BYTES = 1024 * 1024;
const DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES = 64 * 1024 * 1024;

function compressionBackendUnavailable(operation) {
    throw codecError(
        "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE",
        `${operation} requires the zlib-backed __sloppy.codec V8 bridge.`,
    );
}

const COMPRESSION_BRIDGE_ERROR_CODES = new Set([
    "SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE",
    "SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED",
    "SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT",
]);

const COMPRESSION_BRIDGE_ERROR_MESSAGES = Object.freeze({
    SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE: "Compression backend unavailable.",
    SLOPPY_E_CODEC_DECOMPRESSION_LIMIT_EXCEEDED: "Decompression output limit exceeded.",
    SLOPPY_E_CODEC_COMPRESSED_STREAM_CORRUPT: "Compressed stream is corrupt.",
});

function normalizeCompressionError(error) {
    if (error instanceof CodecError) {
        return error;
    }
    const message = typeof error?.message === "string" ? error.message : String(error);
    const match = /\b(SLOPPY_E_CODEC_[A-Z_]+)\b(?::\s*)?(.*)$/u.exec(message);
    if (match !== null && COMPRESSION_BRIDGE_ERROR_CODES.has(match[1])) {
        const normalized = codecError(
            match[1],
            COMPRESSION_BRIDGE_ERROR_MESSAGES[match[1]] ?? "Compression backend failed.",
        );
        normalized.cause = error;
        return normalized;
    }
    return error;
}

function nativeCodec(operation) {
    const bridge = globalThis.__sloppy?.codec ?? null;
    if (bridge === null) {
        compressionBackendUnavailable(operation);
    }
    return bridge;
}

function nativeCompressionFunction(operation, name) {
    const method = nativeCodec(operation)[name];
    if (typeof method !== "function") {
        compressionBackendUnavailable(operation);
    }
    return method;
}

function requireCompressionLevel(value, operation) {
    if (!Number.isInteger(value) || value < 0 || value > 9) {
        throw new TypeError(`${operation} level must be an integer from 0 to 9.`);
    }
    return value;
}

function requireCompressionLimit(value, operation, maximum) {
    if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
        throw new TypeError(`${operation} must be a non-negative safe integer no greater than ${maximum}.`);
    }
    return value;
}

function parseGzipOptions(options, operation) {
    options = validateOptionsObject(options, operation);
    rejectUnknownOptions(options, new Set(["level"]), operation);
    return {
        level: options.level === undefined ? DEFAULT_COMPRESSION_LEVEL : requireCompressionLevel(options.level, operation),
    };
}

function parseGunzipOptions(options, operation) {
    options = validateOptionsObject(options, operation);
    rejectUnknownOptions(options, new Set(["maxOutputBytes"]), operation);
    return {
        maxOutputBytes:
            options.maxOutputBytes === undefined
                ? DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES
                : requireCompressionLimit(options.maxOutputBytes, `${operation} maxOutputBytes`, DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES),
    };
}

function parseCompressionStreamOptions(options, operation, allowedCodecOptions) {
    options = validateOptionsObject(options, operation);
    rejectUnknownOptions(options, new Set([...allowedCodecOptions, "signal", "deadline", "maxInputBytes"]), operation);
    const maxInputBytes =
        options.maxInputBytes === undefined
            ? MAX_COMPRESSION_INPUT_BYTES
            : requireCompressionLimit(options.maxInputBytes, `${operation} maxInputBytes`, MAX_COMPRESSION_INPUT_BYTES);
    const codecOptions = {};
    for (const key of allowedCodecOptions) {
        if (Object.prototype.hasOwnProperty.call(options, key)) {
            if (key === "level") {
                codecOptions.level = requireCompressionLevel(options.level, operation);
            } else if (key === "maxOutputBytes") {
                codecOptions.maxOutputBytes = requireCompressionLimit(
                    options.maxOutputBytes,
                    `${operation} maxOutputBytes`,
                    DEFAULT_DECOMPRESSION_MAX_OUTPUT_BYTES,
                );
            }
        }
    }
    return {
        codecOptions,
        signal: options.signal,
        deadline: options.deadline,
        maxInputBytes,
    };
}

function normalizeCompressionResult(value, operation) {
    if (!(value instanceof Uint8Array)) {
        throw new TypeError(`${operation} native backend must return Uint8Array bytes.`);
    }
    return value.slice();
}

function isCancellationSignal(value) {
    return (
        value !== null &&
        typeof value === "object" &&
        typeof value.aborted === "boolean" &&
        ("reason" in value || typeof value.addEventListener === "function")
    );
}

function subscribeCancellation(signal, listener) {
    if (!isCancellationSignal(signal)) {
        return () => {};
    }
    if (signal.aborted) {
        listener(signal.reason);
        return () => {};
    }
    if (typeof signal._subscribe === "function") {
        return signal._subscribe(listener);
    }
    if (typeof signal.addEventListener === "function") {
        const wrapped = () => listener(signal.reason);
        signal.addEventListener("abort", wrapped);
        return () => signal.removeEventListener?.("abort", wrapped);
    }
    return () => {};
}

function compressionCancelledError(reason = undefined) {
    const error = new Error("Sloppy codec compression operation was cancelled.");
    error.name = "CancelledError";
    if (reason !== undefined) {
        error.reason = reason;
    }
    return error;
}

function compressionTimeoutError(reason = undefined) {
    const error = new Error("Sloppy codec compression operation exceeded its deadline.");
    error.name = "TimeoutError";
    if (reason !== undefined) {
        error.reason = reason;
    }
    return error;
}

function deadlineRemainingMs(deadline, operation) {
    if (deadline === undefined || deadline === null) {
        return Infinity;
    }
    if (typeof deadline.remainingMs !== "function") {
        throw new TypeError(`${operation} deadline must come from Deadline.after, Deadline.at, or Deadline.never.`);
    }
    return deadline.remainingMs();
}

function checkCompressionTerminalOptions(options, operation) {
    if (isCancellationSignal(options.signal) && options.signal.aborted) {
        throw compressionCancelledError(options.signal.reason);
    }
    if (deadlineRemainingMs(options.deadline, operation) <= 0) {
        throw compressionTimeoutError(options.deadline);
    }
}

function raceCompressionTerminal(promise, options, operation) {
    checkCompressionTerminalOptions(options, operation);
    const signal = options.signal;
    const remainingMs = deadlineRemainingMs(options.deadline, operation);
    if (!isCancellationSignal(signal) && remainingMs === Infinity) {
        return promise;
    }
    return new Promise((resolve, reject) => {
        let finished = false;
        let timeoutId;
        let cleanupSignal = () => {};
        const finish = (callback, value) => {
            if (finished) {
                return;
            }
            finished = true;
            cleanupAll();
            callback(value);
        };
        cleanupSignal = subscribeCancellation(signal, (reason) => {
            finish(reject, compressionCancelledError(reason));
        });
        if (remainingMs !== Infinity) {
            const setTimer = globalThis["setTimeout"];
            const clearTimer = globalThis["clearTimeout"];
            if (typeof setTimer !== "function" || typeof clearTimer !== "function") {
                finish(reject, compressionTimeoutError(options.deadline));
                return;
            }
            timeoutId = setTimer(
                () => finish(reject, compressionTimeoutError(options.deadline)),
                Math.min(Math.ceil(remainingMs), 0x7fffffff),
            );
        }
        promise.then(
            (value) => {
                finish(resolve, value);
            },
            (error) => {
                finish(reject, error);
            },
        );
        function cleanupAll() {
            cleanupSignal();
            if (timeoutId !== undefined) {
                const clearTimer = globalThis["clearTimeout"];
                clearTimer(timeoutId);
            }
        }
    });
}

function runCompression(operation, bytes, options, invoke) {
    try {
        bytes = requireBytes(bytes, operation);
        checkCompressionTerminalOptions({ signal: options?.signal, deadline: options?.deadline }, operation);
        if (bytes.byteLength > MAX_COMPRESSION_INPUT_BYTES) {
            throw new TypeError(`${operation} input exceeds the ${MAX_COMPRESSION_INPUT_BYTES} byte inline compression limit.`);
        }
        const promise = Promise.resolve(invoke(new Uint8Array(bytes))).then(
            (result) => normalizeCompressionResult(result, operation),
            (error) => Promise.reject(normalizeCompressionError(error)),
        );
        return raceCompressionTerminal(promise, options ?? {}, operation);
    } catch (error) {
        return Promise.reject(normalizeCompressionError(error));
    }
}

function isIterable(value) {
    return value !== null && typeof value === "object" && (Symbol.asyncIterator in value || Symbol.iterator in value);
}

async function* compressionStream(input, options, operation, parseOptions, invoke) {
    const parsed = parseOptions(options, operation);
    checkCompressionTerminalOptions(parsed, operation);
    let total = 0;
    const chunks = [];
    let terminal = false;
    const iterator =
        typeof input[Symbol.asyncIterator] === "function"
            ? input[Symbol.asyncIterator]()
            : input[Symbol.iterator]();
    try {
        while (true) {
            checkCompressionTerminalOptions(parsed, operation);
            const next = await raceCompressionTerminal(Promise.resolve(iterator.next()), parsed, operation);
            if (next.done === true) {
                break;
            }
            const chunk = next.value;
            const bytes = requireBytes(chunk, operation);
            if (bytes.byteLength > parsed.maxInputBytes - total) {
                throw new TypeError(`${operation} buffered input exceeds maxInputBytes.`);
            }
            chunks.push(new Uint8Array(bytes));
            total += bytes.byteLength;
        }
        checkCompressionTerminalOptions(parsed, operation);
        const inputBytes = new Uint8Array(total);
        let offset = 0;
        for (const chunk of chunks) {
            inputBytes.set(chunk, offset);
            offset += chunk.byteLength;
        }
        const nativeOutput = Promise.resolve(invoke(inputBytes, parsed.codecOptions)).catch((error) =>
            Promise.reject(normalizeCompressionError(error)),
        );
        const output = await raceCompressionTerminal(nativeOutput, parsed, operation);
        checkCompressionTerminalOptions(parsed, operation);
        terminal = true;
        yield output;
    } finally {
        if (!terminal && typeof iterator.return === "function") {
            try {
                await raceCompressionTerminal(Promise.resolve(iterator.return()), parsed, operation);
            } catch {
            }
        }
        chunks.length = 0;
        if (!terminal) {
            total = 0;
        }
    }
}

const Compression = Object.freeze({
    gzip(bytes, options = undefined) {
        try {
            const parsed = parseGzipOptions(options, "Compression.gzip");
            return runCompression("Compression.gzip", bytes, {}, (input) =>
                nativeCompressionFunction("Compression.gzip", "gzip").call(undefined, input, parsed.level),
            );
        } catch (error) {
            return Promise.reject(error);
        }
    },
    gunzip(bytes, options = undefined) {
        try {
            const parsed = parseGunzipOptions(options, "Compression.gunzip");
            return runCompression("Compression.gunzip", bytes, {}, (input) =>
                nativeCompressionFunction("Compression.gunzip", "gunzip").call(undefined, input, parsed.maxOutputBytes),
            );
        } catch (error) {
            return Promise.reject(error);
        }
    },
    gzipStream(input, options = undefined) {
        if (!isIterable(input)) {
            throw new TypeError("Compression.gzipStream input must be an iterable or async iterable of Uint8Array chunks.");
        }
        return compressionStream(input, options, "Compression.gzipStream", (streamOptions, operation) =>
            parseCompressionStreamOptions(streamOptions, operation, ["level"]), (bytes, codecOptions) =>
            Compression.gzip(bytes, codecOptions),
        );
    },
    gunzipStream(input, options = undefined) {
        if (!isIterable(input)) {
            throw new TypeError("Compression.gunzipStream input must be an iterable or async iterable of Uint8Array chunks.");
        }
        return compressionStream(input, options, "Compression.gunzipStream", (streamOptions, operation) =>
            parseCompressionStreamOptions(streamOptions, operation, ["maxOutputBytes"]), (bytes, codecOptions) =>
            Compression.gunzip(bytes, codecOptions),
        );
    },
});

function makeCrc32Table() {
    const table = new Uint32Array(256);
    for (let index = 0; index < table.length; index += 1) {
        let value = index;
        for (let bit = 0; bit < 8; bit += 1) {
            value = (value >>> 1) ^ (value & 1 ? CRC32_POLYNOMIAL_REFLECTED : 0);
        }
        table[index] = value >>> 0;
    }
    return table;
}

// CRC-32/ISO-HDLC uses the reflected IEEE 802.3 polynomial, all-ones init, and final xor.
const CRC32_POLYNOMIAL_REFLECTED = 0xedb88320;
const CRC32_INITIAL = 0xffffffff;
const CRC32_FINAL_XOR = 0xffffffff;
const CRC32_TABLE = makeCrc32Table();

function crc32(bytes) {
    bytes = requireBytes(bytes, "Checksums.crc32");
    let crc = CRC32_INITIAL;
    for (let index = 0; index < bytes.byteLength; index += 1) {
        crc = (crc >>> 8) ^ CRC32_TABLE[(crc ^ bytes[index]) & 0xff];
    }
    return (crc ^ CRC32_FINAL_XOR) >>> 0;
}

const Checksums = Object.freeze({
    crc32,
});

export { Base64, Base64Url, Binary, Checksums, Compression, Hex, Text };
