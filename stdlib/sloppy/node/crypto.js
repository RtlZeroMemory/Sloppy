import { Random } from "../crypto.js";

function randomBytes(size) {
    return Random.bytes(size);
}

function randomUUID() {
    return Random.uuid();
}

function unsupported(name) {
    return () => {
        throw new Error(`SLOPPY_E_NODE_CRYPTO_UNSUPPORTED: node:crypto.${name} is not implemented. Use sloppy/crypto directly.`);
    };
}

const createHash = unsupported("createHash");
const createHmac = unsupported("createHmac");

export { createHash, createHmac, randomBytes, randomUUID };
export default { createHash, createHmac, randomBytes, randomUUID };
