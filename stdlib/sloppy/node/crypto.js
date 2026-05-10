import { ConstantTime, Hash, Hmac, Random, Secret } from "../crypto.js";

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

function createHmac(algorithm, key) {
    const normalized = normalizeAlgorithm(algorithm, "createHmac");
    if (normalized !== "sha256") {
        throw new TypeError("SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.createHmac currently supports sha256.");
    }
    const chunks = [];
    return {
        update(value) {
            chunks.push(value);
            return this;
        },
        async digest(encoding = undefined) {
            const text = chunks.map((chunk) => typeof chunk === "string" ? chunk : String(chunk)).join("");
            const bytes = await Hmac.sha256(Secret.fromUtf8(String(key)), text);
            if (encoding === "hex") {
                return [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");
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
