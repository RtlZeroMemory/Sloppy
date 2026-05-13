import assert from "node:assert/strict";
import crypto from "node:crypto";

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
    const payload = "{}";
    const signed = await Webhooks.sign(payload, {
        secret: "fuzz-secret",
        event: "order.created",
        timestamp: "2000",
        attempt: 1,
    });
    const cases = [
        { signature: signed.headers["Sloppy-Webhook-Signature"], shouldPass: true },
        { signature: "garbage, v1=00", shouldPass: false },
        { signature: signed.headers["Sloppy-Webhook-Signature"].toUpperCase(), shouldPass: false },
        { signature: "v1=00", shouldPass: false },
        { signature: "", shouldPass: false },
    ];
    for (const { signature, shouldPass } of cases) {
        const request = {
            headers: new Map([
                ["sloppy-webhook-timestamp", "2000"],
                ["sloppy-webhook-signature", signature],
            ]),
            bytes: () => new TextEncoder().encode(payload),
        };
        if (shouldPass) {
            await assert.doesNotReject(() => Webhooks.verify(request, {
                secret: "fuzz-secret",
                toleranceMs: 1000,
                nowMs: 2000000,
            }));
        } else {
            await assert.rejects(
                () => Webhooks.verify(request, {
                    secret: "fuzz-secret",
                    toleranceMs: 1000,
                    nowMs: 2000000,
                }),
                (error) => {
                    assert.match(error?.code, /SLOPPY_E_WEBHOOK_/, `unexpected success for signature ${signature}`);
                    return true;
                },
            );
        }
    }
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
