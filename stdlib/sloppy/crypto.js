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

function utf8ToBytes(value) {
    const text = String(value);
    const bytes = [];
    for (let index = 0; index < text.length; index += 1) {
        let codePoint = text.charCodeAt(index);
        if (codePoint >= 0xd800 && codePoint <= 0xdbff && index + 1 < text.length) {
            const next = text.charCodeAt(index + 1);
            if (next >= 0xdc00 && next <= 0xdfff) {
                codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (next - 0xdc00);
                index += 1;
            }
        }
        if (codePoint <= 0x7f) {
            bytes.push(codePoint);
        } else if (codePoint <= 0x7ff) {
            bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
        } else if (codePoint <= 0xffff) {
            bytes.push(
                0xe0 | (codePoint >> 12),
                0x80 | ((codePoint >> 6) & 0x3f),
                0x80 | (codePoint & 0x3f),
            );
        } else {
            bytes.push(
                0xf0 | (codePoint >> 18),
                0x80 | ((codePoint >> 12) & 0x3f),
                0x80 | ((codePoint >> 6) & 0x3f),
                0x80 | (codePoint & 0x3f),
            );
        }
    }
    return new Uint8Array(bytes);
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
        return utf8ToBytes(value);
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

function bytesToHex(bytes) {
    let output = "";
    for (const byte of bytes) {
        output += byte.toString(16).padStart(2, "0");
    }
    return output;
}

function bytesToBase64(bytes) {
    const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let output = "";
    for (let index = 0; index < bytes.byteLength; index += 3) {
        const a = bytes[index];
        const b = index + 1 < bytes.byteLength ? bytes[index + 1] : 0;
        const c = index + 2 < bytes.byteLength ? bytes[index + 2] : 0;
        const triple = (a << 16) | (b << 8) | c;
        output += alphabet[(triple >> 18) & 0x3f];
        output += alphabet[(triple >> 12) & 0x3f];
        output += index + 1 < bytes.byteLength ? alphabet[(triple >> 6) & 0x3f] : "=";
        output += index + 2 < bytes.byteLength ? alphabet[triple & 0x3f] : "=";
    }
    return output;
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
            return bytesToHex(bytes);
        }
        if (encoding === "base64") {
            return bytesToBase64(bytes);
        }
        if (encoding !== undefined && encoding !== "bytes") {
            throw new TypeError("Sloppy Hash.digest encoding must be bytes, hex, or base64.");
        }
        return bytes;
    }
}

class SecretValue {
    constructor(bytes) {
        this._bytes = cloneBytes(bytes);
        this._disposed = false;
        Object.seal(this);
    }

    static fromUtf8(value) {
        if (typeof value !== "string") {
            throw new TypeError("Sloppy Secret.fromUtf8 value must be a string.");
        }
        return new SecretValue(utf8ToBytes(value));
    }

    static fromBytes(value) {
        if (!(value instanceof Uint8Array)) {
            throw new TypeError("Sloppy Secret.fromBytes value must be a Uint8Array.");
        }
        return new SecretValue(value);
    }

    bytes() {
        if (this._disposed) {
            throw new Error("SLOPPY_E_CRYPTO_SECRET_DISPOSED: secret has been disposed");
        }
        return cloneBytes(this._bytes);
    }

    dispose() {
        if (!this._disposed) {
            this._bytes.fill(0);
            this._disposed = true;
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
        return bytesToHex(digest("sha256", value, "Hash.sha256Hex"));
    },
    async sha256Base64(value) {
        return bytesToBase64(digest("sha256", value, "Hash.sha256Base64"));
    },
    create(algorithm) {
        return new IncrementalHasher(algorithm);
    },
});

const Hmac = Object.freeze({
    sha256(secret, value) {
        return Promise.resolve(hmac("sha256", secret, value, "Hmac.sha256"));
    },
    async verifySha256(secret, value, signature) {
        const actual = hmac("sha256", secret, value, "Hmac.verifySha256");
        const expected = dataToBytes(signature, "Hmac.verifySha256");
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
    xxHash64() {
        throw new Error("SLOPPY_E_CRYPTO_BACKEND_UNAVAILABLE: NonCryptoHash.xxHash64 lands in CORE-CRYPTO-01.G.");
    },
});

const Secret = Object.freeze({
    fromUtf8: SecretValue.fromUtf8,
    fromBytes: SecretValue.fromBytes,
});

export { ConstantTime, Hash, Hmac, NonCryptoHash, Password, Random, Secret };
