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

function validateOptionsObject(options, operation) {
    if (options === undefined) {
        return {};
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
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

const Binary = Object.freeze({
    reader() {
        throw codecError("SLOPPY_E_CODEC_FEATURE_UNAVAILABLE", "Binary.reader lands in CORE-CODEC-01.E.");
    },
    writer() {
        throw codecError("SLOPPY_E_CODEC_FEATURE_UNAVAILABLE", "Binary.writer lands in CORE-CODEC-01.E.");
    },
});

const Compression = Object.freeze({
    gzip() {
        return Promise.reject(codecError("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE", "Compression.gzip lands in CORE-CODEC-01.F."));
    },
    gunzip() {
        return Promise.reject(codecError("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE", "Compression.gunzip lands in CORE-CODEC-01.F."));
    },
    gzipStream() {
        throw codecError("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE", "Compression.gzipStream lands in CORE-CODEC-01.G.");
    },
    gunzipStream() {
        throw codecError("SLOPPY_E_CODEC_COMPRESSION_BACKEND_UNAVAILABLE", "Compression.gunzipStream lands in CORE-CODEC-01.G.");
    },
});

const Checksums = Object.freeze({
    crc32() {
        throw codecError("SLOPPY_E_CODEC_CHECKSUM_UNSUPPORTED_ALGORITHM", "Checksums.crc32 lands in CORE-CODEC-01.H.");
    },
});

export { Base64, Base64Url, Binary, Checksums, Compression, Hex, Text };
