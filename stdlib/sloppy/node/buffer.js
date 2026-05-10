import { Text } from "../codec.js";
import { Base64, Hex } from "../codec.js";

function normalizeEncoding(encoding = "utf8") {
    const normalized = String(encoding).toLowerCase();
    if (normalized === "utf-8") {
        return "utf8";
    }
    if (["utf8", "hex", "base64"].includes(normalized)) {
        return normalized;
    }
    throw new TypeError(`Sloppy Buffer encoding is not supported: ${encoding}`);
}

function bytesFromString(value, encoding) {
    switch (normalizeEncoding(encoding)) {
        case "utf8":
            return Text.utf8.encode(value);
        case "hex":
            return Hex.decode(value);
        case "base64":
            return Base64.decode(value);
        default:
            throw new TypeError(`Sloppy Buffer encoding is not supported: ${encoding}`);
    }
}

function bytesToString(value, encoding) {
    switch (normalizeEncoding(encoding)) {
        case "utf8":
            return Text.utf8.decode(value);
        case "hex":
            return Hex.encode(value);
        case "base64":
            return Base64.encode(value);
        default:
            throw new TypeError(`Sloppy Buffer encoding is not supported: ${encoding}`);
    }
}

class Buffer extends Uint8Array {
    static from(value, encoding = "utf8") {
        if (typeof value === "string") {
            return new Buffer(bytesFromString(value, encoding));
        }
        if (value instanceof ArrayBuffer || ArrayBuffer.isView(value) || Array.isArray(value)) {
            return new Buffer(value);
        }
        throw new TypeError("Sloppy Buffer.from expects a string, bytes, or array.");
    }

    static alloc(size, fill = 0) {
        const buffer = new Buffer(size);
        buffer.fill(fill);
        return buffer;
    }

    static allocUnsafe(size) {
        return Buffer.alloc(size);
    }

    static isBuffer(value) {
        return value instanceof Buffer;
    }

    static byteLength(value, encoding = "utf8") {
        return Buffer.from(value, encoding).byteLength;
    }

    static concat(list, totalLength = undefined) {
        if (!Array.isArray(list)) {
            throw new TypeError("Sloppy Buffer.concat expects an array.");
        }
        const buffers = list.map((entry) => Buffer.from(entry));
        const length = totalLength === undefined
            ? buffers.reduce((sum, entry) => sum + entry.byteLength, 0)
            : Number(totalLength);
        if (!Number.isInteger(length) || length < 0) {
            throw new TypeError("Sloppy Buffer.concat totalLength must be a non-negative integer.");
        }
        const output = new Buffer(length);
        let offset = 0;
        for (const buffer of buffers) {
            output.set(buffer.subarray(0, Math.max(0, length - offset)), offset);
            offset += buffer.byteLength;
            if (offset >= length) {
                break;
            }
        }
        return output;
    }

    equals(other) {
        const buffer = Buffer.from(other);
        if (buffer.byteLength !== this.byteLength) {
            return false;
        }
        for (let index = 0; index < this.byteLength; index += 1) {
            if (this[index] !== buffer[index]) {
                return false;
            }
        }
        return true;
    }

    subarray(start = 0, end = undefined) {
        return new Buffer(super.subarray(start, end));
    }

    slice(start = 0, end = undefined) {
        return this.subarray(start, end);
    }

    toString(encoding = "utf8") {
        return bytesToString(this, encoding);
    }
}

export { Buffer };
export default { Buffer };
