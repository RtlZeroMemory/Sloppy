import assert from "node:assert/strict";
import crypto from "node:crypto";

const previousRuntime = globalThis.__sloppy_runtime;
const previousSloppy = globalThis.__sloppy;
globalThis.__sloppy = {
    ...(previousSloppy ?? {}),
    crypto: {
        ...(previousSloppy?.crypto ?? {}),
        randomUuid: () => crypto.randomUUID(),
        randomBytes: (length) => new Uint8Array(crypto.randomBytes(length)),
        hmac: (algorithm, key, bytes) =>
            new Uint8Array(crypto.createHmac(algorithm, Buffer.from(key)).update(Buffer.from(bytes)).digest()),
    },
};
await import("../../stdlib/sloppy/internal/runtime-classic.js");

try {
    const { Secret, Webhooks } = globalThis.__sloppy_runtime;

    assert.throws(() => Webhooks.retry.fixed({ maxAttempts: 0 }), /maxAttempts/u);
    assert.throws(() => Webhooks.retry.fixed({ delayMs: -1 }), /delayMs/u);
    assert.throws(() => Webhooks.retry.fixed({ retryOnStatus: [99] }), /retryOnStatus/u);
    assert.throws(() => Webhooks.retry.fixed({ jitter: "yes" }), /jitter/u);
    assert.throws(() => Webhooks.retry.exponential({ initialDelayMs: 10, maxDelayMs: 1 }), /maxDelayMs/u);

    await assert.rejects(
        () => Webhooks.sign(undefined, { secret: "runtime-secret", event: "order.created" }),
        /SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED/u,
    );
    const circular = {};
    circular.self = circular;
    await assert.rejects(
        () => Webhooks.sign(circular, { secret: "runtime-secret", event: "order.created" }),
        /SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED/u,
    );

    const callerSecret = Secret.fromUtf8("runtime-secret");
    await Webhooks.sign("{}", { secret: callerSecret, event: "order.created", timestamp: "2000" });
    assert.doesNotThrow(() => callerSecret.bytes());
    callerSecret.dispose();

    const signed = await Webhooks.sign("{}", { secret: "runtime-secret", event: "order.created", timestamp: "2000" });
    assert.match(signed.signature, /^v1=/u);
} finally {
    if (previousRuntime === undefined) {
        delete globalThis.__sloppy_runtime;
    } else {
        globalThis.__sloppy_runtime = previousRuntime;
    }
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
