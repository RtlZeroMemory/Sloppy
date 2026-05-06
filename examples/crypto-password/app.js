import { Password } from "sloppy/crypto";

export async function createPasswordRecord(password) {
    const encodedHash = await Password.hash(password);
    return { encodedHash };
}

export async function verifyPasswordRecord(password, encodedHash) {
    const ok = await Password.verify(password, encodedHash);
    const shouldUpgrade = await Password.needsRehash(encodedHash);
    return { ok, shouldUpgrade };
}
