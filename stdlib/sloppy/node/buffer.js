import { Text } from "../codec.js";

class Buffer extends Uint8Array {
    static from(value, encoding = "utf8") {
        if (typeof value === "string") {
            if (encoding !== "utf8" && encoding !== "utf-8") {
                throw new TypeError("Sloppy Buffer.from currently supports utf8 strings.");
            }
            return new Buffer(Text.utf8.encode(value));
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

    static isBuffer(value) {
        return value instanceof Buffer;
    }

    static byteLength(value, encoding = "utf8") {
        return Buffer.from(value, encoding).byteLength;
    }

    toString(encoding = "utf8") {
        if (encoding !== "utf8" && encoding !== "utf-8") {
            throw new TypeError("Sloppy Buffer.toString currently supports utf8.");
        }
        return Text.utf8.decode(this);
    }
}

export { Buffer };
export default { Buffer };
