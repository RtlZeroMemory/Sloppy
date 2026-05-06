import { Compression, Text } from "sloppy/codec";

const body = Text.utf8.encode("compress me");

export async function roundtrip() {
    const gz = await Compression.gzip(body, { level: 6 });
    const raw = await Compression.gunzip(gz, { maxOutputBytes: 1024 * 1024 });
    return Text.utf8.decode(raw, { fatal: true });
}
