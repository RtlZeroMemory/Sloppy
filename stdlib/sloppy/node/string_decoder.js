import { Text } from "../codec.js";

class StringDecoder {
    constructor(encoding = "utf8") {
        const normalized = String(encoding).toLowerCase();
        if (normalized !== "utf8" && normalized !== "utf-8") {
            throw new TypeError("Sloppy string_decoder only supports utf8.");
        }
        this._decoder = Text.utf8.decoder();
    }

    write(buffer) {
        return this._decoder.decode(buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer), { stream: true });
    }

    end(buffer = undefined) {
        const text = buffer === undefined ? "" : this.write(buffer);
        return text + this._decoder.finish();
    }
}

export { StringDecoder };
export default { StringDecoder };
