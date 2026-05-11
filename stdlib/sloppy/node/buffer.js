import { Base64, Base64Url, Hex, Text } from "../codec.js";

function normalizeEncoding(encoding = "utf8") {
    const normalized = String(encoding).toLowerCase();
    if (normalized === "utf-8") {
        return "utf8";
    }
    if (["utf8", "hex", "base64", "base64url"].includes(normalized)) {
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
        case "base64url":
            return Base64Url.decode(value, { padding: "optional" });
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
        case "base64url":
            return Base64Url.encode(value);
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

    static compare(left, right) {
        left = Buffer.from(left);
        right = Buffer.from(right);
        const length = Math.min(left.byteLength, right.byteLength);
        for (let index = 0; index < length; index += 1) {
            if (left[index] !== right[index]) {
                return left[index] < right[index] ? -1 : 1;
            }
        }
        if (left.byteLength === right.byteLength) {
            return 0;
        }
        return left.byteLength < right.byteLength ? -1 : 1;
    }

    static isEncoding(encoding) {
        if (encoding === undefined || encoding === null) {
            return false;
        }
        try {
            normalizeEncoding(encoding);
            return true;
        } catch {
            return false;
        }
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
        const view = super.subarray(start, end);
        Object.setPrototypeOf(view, Buffer.prototype);
        return view;
    }

    slice(start = 0, end = undefined) {
        return this.subarray(start, end);
    }

    compare(other) {
        return Buffer.compare(this, other);
    }

    readUInt8(offset = 0) {
        return this.#readUInt(offset, 1);
    }

    readUInt16LE(offset = 0) {
        return this.#readUInt(offset, 2, true);
    }

    readUInt16BE(offset = 0) {
        return this.#readUInt(offset, 2, false);
    }

    readUInt32LE(offset = 0) {
        return this.#readUInt(offset, 4, true);
    }

    readUInt32BE(offset = 0) {
        return this.#readUInt(offset, 4, false);
    }

    writeUInt8(value, offset = 0) {
        return this.#writeUInt(value, offset, 1);
    }

    writeUInt16LE(value, offset = 0) {
        return this.#writeUInt(value, offset, 2, true);
    }

    writeUInt16BE(value, offset = 0) {
        return this.#writeUInt(value, offset, 2, false);
    }

    writeUInt32LE(value, offset = 0) {
        return this.#writeUInt(value, offset, 4, true);
    }

    writeUInt32BE(value, offset = 0) {
        return this.#writeUInt(value, offset, 4, false);
    }

    write(value, offset = 0, length = undefined, encoding = "utf8") {
        if (typeof offset === "string") {
            encoding = offset;
            offset = 0;
            length = undefined;
        } else if (typeof length === "string") {
            encoding = length;
            length = undefined;
        }
        const bytes = Buffer.from(String(value), encoding);
        const start = Number(offset);
        const available = this.byteLength - start;
        const count = Math.min(length === undefined ? bytes.byteLength : Number(length), available);
        if (!Number.isInteger(start) || start < 0 || !Number.isInteger(count) || count < 0) {
            throw new RangeError("Sloppy Buffer.write offset and length must be non-negative integers.");
        }
        this.set(bytes.subarray(0, count), start);
        return count;
    }

    toString(encoding = "utf8") {
        return bytesToString(this, encoding);
    }

    #checkBounds(offset, width) {
        offset = Number(offset);
        if (!Number.isInteger(offset) || offset < 0 || offset + width > this.byteLength) {
            throw new RangeError("Sloppy Buffer offset is outside the buffer bounds.");
        }
        return offset;
    }

    #readUInt(offset, width, littleEndian = true) {
        offset = this.#checkBounds(offset, width);
        let value = 0;
        for (let index = 0; index < width; index += 1) {
            const byte = this[offset + (littleEndian ? index : width - 1 - index)];
            value += byte * 2 ** (8 * index);
        }
        return value;
    }

    #writeUInt(value, offset, width, littleEndian = true) {
        offset = this.#checkBounds(offset, width);
        value = Number(value);
        const max = 2 ** (8 * width);
        if (!Number.isInteger(value) || value < 0 || value >= max) {
            throw new RangeError("Sloppy Buffer unsigned integer value is outside the supported range.");
        }
        for (let index = 0; index < width; index += 1) {
            this[offset + (littleEndian ? index : width - 1 - index)] = Math.floor(value / 2 ** (8 * index)) & 0xff;
        }
        return offset + width;
    }
}

export { Buffer };
export default { Buffer };
