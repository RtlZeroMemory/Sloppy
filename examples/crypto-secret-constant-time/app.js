import { ConstantTime, Hmac, Secret } from "sloppy/crypto";

export async function verifyWebhook(body, providedSignature, configuredKeyText) {
    const key = Secret.fromUtf8(configuredKeyText);
    try {
        const expectedSignature = await Hmac.sha256(key, body);
        return ConstantTime.equals(expectedSignature, providedSignature);
    } finally {
        key.dispose();
    }
}

export function verifyOpaqueBytes(left, right) {
    return ConstantTime.equals(left, right);
}
