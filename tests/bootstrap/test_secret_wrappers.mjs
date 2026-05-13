import assert from "node:assert/strict";

import { Secret } from "../../stdlib/sloppy/crypto.js";
import { createConfigProvider } from "../../stdlib/sloppy/internal/config.js";

await import("../../stdlib/sloppy/internal/runtime-classic.js");
const runtimeClassic = globalThis.__sloppy_runtime;

{
    const secret = Secret.fromUtf8("top-secret");
    assert.deepEqual(Object.keys(secret), []);
    assert.deepEqual({ ...secret }, {});
    assert.equal(String(secret), "[Secret redacted]");
    assert.equal(JSON.stringify(secret), "\"[Secret redacted]\"");
    assert.deepEqual(Array.from(secret.bytes()), Array.from(new TextEncoder().encode("top-secret")));
    secret.dispose();
    assert.throws(() => secret.bytes(), /SLOPPY_E_CRYPTO_SECRET_DISPOSED/);
}

{
    const config = createConfigProvider({ "AUTH:SESSIONSECRET": "session-secret" });
    const secret = config.getSecret("Auth:SessionSecret");
    assert.deepEqual(Object.keys(secret), []);
    assert.deepEqual({ ...secret }, {});
    assert.equal(secret.value(), "session-secret");
    assert.equal(String(secret), "[Secret redacted]");
    assert.equal(JSON.stringify(secret), "\"[Secret redacted]\"");
}

{
    const secret = runtimeClassic.Secret.fromUtf8("classic-secret");
    assert.deepEqual(Object.keys(secret), []);
    assert.deepEqual({ ...secret }, {});
    assert.equal(String(secret), "[Secret redacted]");
    assert.deepEqual(Array.from(secret.bytes()), Array.from(new TextEncoder().encode("classic-secret")));
    secret.dispose();
    assert.throws(() => secret.bytes(), /SLOPPY_E_CRYPTO_SECRET_DISPOSED/);
}
