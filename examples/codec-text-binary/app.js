import { Binary, Text } from "sloppy/codec";

const payload = Text.utf8.encode("hello\0codec");

const writer = Binary.writer();
writer.u32le(1);
writer.u16be(payload.length);
writer.bytes(payload);

const packet = writer.toBytes();
const reader = Binary.reader(packet);

export const decoded = {
    version: reader.u32le(),
    length: reader.u16be(),
    text: Text.utf8.decode(reader.bytes(payload.length), { fatal: true }),
};
