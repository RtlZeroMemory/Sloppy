import { Checksums, Text } from "sloppy/codec";

const payload = Text.utf8.encode("cache record");

export const record = {
    bytes: payload.byteLength,
    crc32: Checksums.crc32(payload),
};
