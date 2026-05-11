import { ConstantTime, Hash, Hmac, Random, Secret } from "../crypto.js";
import { Hex, Text } from "../codec.js";

function randomBytes(size) {
    return Random.bytes(size);
}

function randomUUID() {
    return Random.uuid();
}

function normalizeAlgorithm(algorithm, operation) {
    const normalized = String(algorithm).toLowerCase().replace(/-/g, "");
    if (normalized === "sha256") {
        return "sha256";
    }
    if (normalized === "sha384") {
        return "sha384";
    }
    if (normalized === "sha512") {
        return "sha512";
    }
    throw new TypeError(`SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.${operation} only supports sha256, sha384, and sha512.`);
}

function createHash(algorithm) {
    return Hash.create(normalizeAlgorithm(algorithm, "createHash"));
}

function bytesFrom(value, operation) {
    if (typeof value === "string") {
        return Text.utf8.encode(value);
    }
    if (value instanceof ArrayBuffer) {
        return new Uint8Array(value).slice();
    }
    if (ArrayBuffer.isView(value)) {
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
    }
    throw new TypeError(`${operation} requires a string, ArrayBuffer, or typed-array bytes.`);
}

function secretFrom(value) {
    return typeof value === "string"
        ? Secret.fromUtf8(value)
        : Secret.fromBytes(bytesFrom(value, "node:crypto.createHmac key"));
}

function createHmac(algorithm, key) {
    const normalized = normalizeAlgorithm(algorithm, "createHmac");
    if (normalized !== "sha256") {
        throw new TypeError("SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.createHmac currently supports sha256.");
    }
    const secret = secretFrom(key);
    const chunks = [];
    return {
        update(value) {
            chunks.push(bytesFrom(value, "node:crypto.Hmac.update"));
            return this;
        },
        async digest(encoding = undefined) {
            const length = chunks.reduce((total, chunk) => total + chunk.byteLength, 0);
            const input = new Uint8Array(length);
            let offset = 0;
            for (const chunk of chunks) {
                input.set(chunk, offset);
                offset += chunk.byteLength;
            }
            const bytes = await Hmac.sha256(secret, input);
            if (encoding === "hex") {
                return Hex.encode(bytes);
            }
            if (encoding !== undefined && encoding !== "bytes") {
                throw new TypeError("Sloppy node:crypto Hmac digest encoding must be bytes or hex.");
            }
            return bytes;
        },
    };
}

function timingSafeEqual(left, right) {
    if (left.byteLength !== right.byteLength) {
        throw new RangeError("Input buffers must have the same byte length.");
    }
    return ConstantTime.equals(left, right);
}

export { createHash, createHmac, randomBytes, randomUUID, timingSafeEqual };
export default { createHash, createHmac, randomBytes, randomUUID, timingSafeEqual };
