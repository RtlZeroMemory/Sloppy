import { Text } from "sloppy/codec";
import { LocalEndpoint } from "sloppy/net";
import { Deadline } from "sloppy/time";

export async function readLocalStatus(signal) {
    const deadline = Deadline.after(1000);
    const endpoint = await LocalEndpoint.connect({
        path: "runtime:/status.sock",
        deadline,
        signal,
    });

    try {
        await endpoint.write(Text.utf8.encode("status\n"), { deadline, signal });
        const bytes = await endpoint.read({ maxBytes: 4096, deadline, signal });
        return Text.utf8.decode(bytes, { fatal: true });
    } finally {
        await endpoint.close();
    }
}
