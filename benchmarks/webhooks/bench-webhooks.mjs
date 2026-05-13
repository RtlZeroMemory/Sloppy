import assert from "node:assert/strict";
import crypto from "node:crypto";
import { performance } from "node:perf_hooks";

import { Webhooks } from "../../stdlib/sloppy/webhooks.js";

const previousSloppy = globalThis.__sloppy;
globalThis.__sloppy = {
    ...(previousSloppy ?? {}),
    crypto: {
        ...(previousSloppy?.crypto ?? {}),
        randomUuid: () => crypto.randomUUID(),
        randomBytes: (length) => new Uint8Array(crypto.randomBytes(length)),
        hmac: (algorithm, key, bytes) =>
            new Uint8Array(crypto.createHmac(algorithm, Buffer.from(key)).update(Buffer.from(bytes)).digest()),
        constantTimeEquals(left, right) {
            const a = Buffer.from(left);
            const b = Buffer.from(right);
            return a.length === b.length && crypto.timingSafeEqual(a, b);
        },
    },
};

try {
    const payload = JSON.stringify({ orderId: "ord_1", total: 42 });
    const started = performance.now();
    for (let index = 0; index < 100; index += 1) {
        const signed = await Webhooks.sign(payload, {
            secret: "bench-secret",
            event: "order.created",
            attempt: 1,
        });
        assert.match(signed.signature, /^v1=/);
    }
    const durationMs = performance.now() - started;
    console.log(JSON.stringify({
        benchmark: "webhooks.signature.sign",
        iterations: 100,
        durationMs,
    }));
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
