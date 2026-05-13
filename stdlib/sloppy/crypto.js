import { Base64, Hex, Text } from "./codec.js";

const MAX_INLINE_BYTES = 1024 * 1024;
const MAX_PASSWORD_BYTES = 4096;
const PASSWORD_DEFAULT_OPS_LIMIT = 2;
const PASSWORD_MIN_OPS_LIMIT = 2;
const PASSWORD_MAX_OPS_LIMIT = 4;
const PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES = 67108864;
const PASSWORD_MIN_MEMORY_LIMIT_BYTES = 67108864;
const PASSWORD_MAX_MEMORY_LIMIT_BYTES = 268435456;

function cryptoUnavailable(operation) {
    throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.crypto is inactive or unavailable

Feature:
  stdlib.crypto

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.crypto V8 intrinsic namespace.`);
}

function nativeCrypto(operation) {
    const bridge = globalThis.__sloppy?.crypto ?? null;
    if (bridge === null) {
        cryptoUnavailable(operation);
    }
    return bridge;
}

function cloneBytes(bytes) {
    return bytes.slice();
}

function dataToBytes(value, operation) {
    if (value instanceof SecretValue) {
        return value.bytes();
    }
    if (value instanceof Uint8Array) {
        return cloneBytes(value);
    }
    if (typeof value === "string") {
        return Text.utf8.encode(value);
    }
    throw new TypeError(`Sloppy crypto ${operation} data must be a string, Uint8Array, or Secret.`);
}

function requireBoundedBytes(bytes, operation) {
    if (bytes.byteLength > MAX_INLINE_BYTES) {
        throw new TypeError(`Sloppy crypto ${operation} input is too large for inline hashing.`);
    }
    return bytes;
}

function passwordBytes(value, operation) {
    const bytes = dataToBytes(value, operation);
    if (bytes.byteLength > MAX_PASSWORD_BYTES) {
        throw new TypeError(`Sloppy crypto ${operation} password input is too large.`);
    }
    return bytes;
}

function passwordOptions(options = undefined) {
    if (options === undefined) {
        return {
            opsLimit: PASSWORD_DEFAULT_OPS_LIMIT,
            memoryLimitBytes: PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES,
        };
    }
    if (options === null || typeof options !== "object") {
        throw new TypeError("Sloppy Password options must be an object when provided.");
    }

    const opsLimit = options.opsLimit ?? PASSWORD_DEFAULT_OPS_LIMIT;
    const memoryLimitBytes = options.memoryLimitBytes ?? PASSWORD_DEFAULT_MEMORY_LIMIT_BYTES;
    if (
        !Number.isInteger(opsLimit) ||
        opsLimit < PASSWORD_MIN_OPS_LIMIT ||
        opsLimit > PASSWORD_MAX_OPS_LIMIT ||
        !Number.isInteger(memoryLimitBytes) ||
        memoryLimitBytes < PASSWORD_MIN_MEMORY_LIMIT_BYTES ||
        memoryLimitBytes > PASSWORD_MAX_MEMORY_LIMIT_BYTES
    ) {
        throw new TypeError("Sloppy Password options are outside the documented safe bounds.");
    }
    return { opsLimit, memoryLimitBytes };
}

function encodedPasswordHash(value, operation) {
    if (typeof value !== "string") {
        throw new TypeError(`Sloppy crypto ${operation} encoded hash must be a string.`);
    }
    return value;
}

function digest(algorithm, value, operation) {
    const bytes = requireBoundedBytes(dataToBytes(value, operation), operation);
    return nativeCrypto(operation).hash(algorithm, bytes);
}

function hmac(algorithm, secret, value, operation) {
    const key = requireBoundedBytes(dataToBytes(secret, operation), operation);
    const bytes = requireBoundedBytes(dataToBytes(value, operation), operation);
    return nativeCrypto(operation).hmac(algorithm, key, bytes);
}

class IncrementalHasher {
    constructor(algorithm) {
        if (!["sha256", "sha384", "sha512"].includes(algorithm)) {
            throw new TypeError("Sloppy Hash.create algorithm must be sha256, sha384, or sha512.");
        }
        this._algorithm = algorithm;
        this._chunks = [];
        this._digested = false;
    }

    update(value) {
        if (this._digested) {
            throw new Error("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM: hasher already digested");
        }
        this._chunks.push(dataToBytes(value, "Hash.update"));
        return this;
    }

    async digest(encoding = undefined) {
        if (this._digested) {
            throw new Error("SLOPPY_E_CRYPTO_UNSUPPORTED_ALGORITHM: hasher already digested");
        }
        this._digested = true;
        const length = this._chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
        const joined = new Uint8Array(length);
        let offset = 0;
        for (const chunk of this._chunks) {
            joined.set(chunk, offset);
            offset += chunk.byteLength;
        }
        const bytes = digest(this._algorithm, joined, "Hash.digest");
        if (encoding === "hex") {
            return Hex.encode(bytes);
        }
        if (encoding === "base64") {
            return Base64.encode(bytes);
        }
        if (encoding !== undefined && encoding !== "bytes") {
            throw new TypeError("Sloppy Hash.digest encoding must be bytes, hex, or base64.");
        }
        return bytes;
    }
}

const SECRET_VALUE_BYTES = new WeakMap();
const SECRET_VALUE_DISPOSED = new WeakSet();

class SecretValue {
    constructor(bytes) {
        SECRET_VALUE_BYTES.set(this, cloneBytes(bytes));
        Object.freeze(this);
    }

    static fromUtf8(value) {
        if (typeof value !== "string") {
            throw new TypeError("Sloppy Secret.fromUtf8 value must be a string.");
        }
        return new SecretValue(Text.utf8.encode(value));
    }

    static fromBytes(value) {
        if (!(value instanceof Uint8Array)) {
            throw new TypeError("Sloppy Secret.fromBytes value must be a Uint8Array.");
        }
        return new SecretValue(value);
    }

    bytes() {
        if (SECRET_VALUE_DISPOSED.has(this)) {
            throw new Error("SLOPPY_E_CRYPTO_SECRET_DISPOSED: secret has been disposed");
        }
        return cloneBytes(SECRET_VALUE_BYTES.get(this));
    }

    dispose() {
        if (!SECRET_VALUE_DISPOSED.has(this)) {
            SECRET_VALUE_BYTES.get(this).fill(0);
            SECRET_VALUE_DISPOSED.add(this);
        }
    }

    toString() {
        return "[Secret redacted]";
    }

    toJSON() {
        return "[Secret redacted]";
    }
}

const Random = Object.freeze({
    bytes(length) {
        return nativeCrypto("Random.bytes").randomBytes(length);
    },
    uuid() {
        return nativeCrypto("Random.uuid").randomUuid();
    },
    token(length) {
        return nativeCrypto("Random.token").randomToken(length);
    },
    hex(length) {
        return nativeCrypto("Random.hex").randomHex(length);
    },
    numericCode(length) {
        return nativeCrypto("Random.numericCode").randomNumericCode(length);
    },
});

const Hash = Object.freeze({
    sha256(value) {
        return Promise.resolve(digest("sha256", value, "Hash.sha256"));
    },
    sha384(value) {
        return Promise.resolve(digest("sha384", value, "Hash.sha384"));
    },
    sha512(value) {
        return Promise.resolve(digest("sha512", value, "Hash.sha512"));
    },
    async sha256Hex(value) {
        return Hex.encode(digest("sha256", value, "Hash.sha256Hex"));
    },
    async sha256Base64(value) {
        return Base64.encode(digest("sha256", value, "Hash.sha256Base64"));
    },
    create(algorithm) {
        return new IncrementalHasher(algorithm);
    },
});

const Hmac = Object.freeze({
    sha256(secret, value) {
        return Promise.resolve(hmac("sha256", secret, value, "Hmac.sha256"));
    },
    sha384(secret, value) {
        return Promise.resolve(hmac("sha384", secret, value, "Hmac.sha384"));
    },
    sha512(secret, value) {
        return Promise.resolve(hmac("sha512", secret, value, "Hmac.sha512"));
    },
    async verifySha256(secret, value, signature) {
        const actual = hmac("sha256", secret, value, "Hmac.verifySha256");
        const expected = dataToBytes(signature, "Hmac.verifySha256");
        return ConstantTime.equals(actual, expected);
    },
    async verifySha384(secret, value, signature) {
        const actual = hmac("sha384", secret, value, "Hmac.verifySha384");
        const expected = dataToBytes(signature, "Hmac.verifySha384");
        return ConstantTime.equals(actual, expected);
    },
    async verifySha512(secret, value, signature) {
        const actual = hmac("sha512", secret, value, "Hmac.verifySha512");
        const expected = dataToBytes(signature, "Hmac.verifySha512");
        return ConstantTime.equals(actual, expected);
    },
});

const ConstantTime = Object.freeze({
    equals(left, right) {
        return nativeCrypto("ConstantTime.equals").constantTimeEquals(
            dataToBytes(left, "ConstantTime.equals"),
            dataToBytes(right, "ConstantTime.equals"),
        );
    },
});

const Password = Object.freeze({
    hash(password, options = undefined) {
        const normalized = passwordOptions(options);
        return nativeCrypto("Password.hash").passwordHash(
            passwordBytes(password, "Password.hash"),
            normalized.opsLimit,
            normalized.memoryLimitBytes,
        );
    },
    verify(password, encodedHash) {
        return nativeCrypto("Password.verify").passwordVerify(
            passwordBytes(password, "Password.verify"),
            encodedPasswordHash(encodedHash, "Password.verify"),
        );
    },
    needsRehash(encodedHash, options = undefined) {
        const normalized = passwordOptions(options);
        return nativeCrypto("Password.needsRehash").passwordNeedsRehash(
            encodedPasswordHash(encodedHash, "Password.needsRehash"),
            normalized.opsLimit,
            normalized.memoryLimitBytes,
        );
    },
});

const NonCryptoHash = Object.freeze({
    xxHash64(data) {
        return nativeCrypto("NonCryptoHash.xxHash64").nonCryptoXxHash64(
            dataToBytes(data, "NonCryptoHash.xxHash64"),
        );
    },
});

const Secret = Object.freeze({
    fromUtf8: SecretValue.fromUtf8,
    fromBytes: SecretValue.fromBytes,
});

export { ConstantTime, Hash, Hmac, NonCryptoHash, Password, Random, Secret };
