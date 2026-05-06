import { Hash, Hmac, Secret } from "sloppy/crypto";

function readSigningKeyFromConfig(config) {
    return config.hmacKey;
}

export async function describePayload(payload, config) {
    const signingKey = Secret.fromUtf8(readSigningKeyFromConfig(config));
    try {
        const sha256Hex = await Hash.sha256Hex(payload);
        const sha256Base64 = await Hash.sha256Base64(payload);
        const hasher = Hash.create("sha256");
        hasher.update("event:");
        hasher.update(payload);
        const incrementalHex = await hasher.digest("hex");
        const signature = await Hmac.sha256(signingKey, payload);
        const signatureOk = await Hmac.verifySha256(signingKey, payload, signature);

        return {
            sha256Hex,
            sha256Base64,
            incrementalHex,
            signatureBytes: signature.byteLength,
            signatureOk,
        };
    } finally {
        signingKey.dispose();
    }
}
