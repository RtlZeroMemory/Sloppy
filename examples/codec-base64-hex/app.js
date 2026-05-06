import { Base64, Base64Url, Hex } from "sloppy/codec";

const bytes = new Uint8Array([0, 15, 16, 255]);

export const encoded = {
    base64: Base64.encode(bytes),
    base64Url: Base64Url.encode(bytes, { padding: false }),
    hex: Hex.encode(bytes),
};

export const decoded = {
    base64: Base64.decode(encoded.base64),
    base64Url: Base64Url.decode(encoded.base64Url, { padding: "optional" }),
    hex: Hex.decode(encoded.hex),
};
