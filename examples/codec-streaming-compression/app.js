import { Compression, Text } from "sloppy/codec";
import { Deadline } from "sloppy/time";

async function* chunks() {
    yield Text.utf8.encode("stream ");
    yield Text.utf8.encode("payload");
}

export async function compressedChunks(signal) {
    const deadline = Deadline.after(1000);
    const output = [];
    for await (const chunk of Compression.gzipStream(chunks(), { signal, deadline })) {
        output.push(chunk);
    }
    return output;
}
