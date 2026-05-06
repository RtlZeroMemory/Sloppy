import { Text } from "sloppy/codec";
import { Process } from "sloppy/os";
import { Deadline } from "sloppy/time";

let statusText = "";

async function refreshStatusText() {
    let process = null;
    try {
        process = await Process.start("status-tool", ["--format=text"], {
            stdout: "pipe",
            deadline: Deadline.after(1000),
        });
        const output = await process.stdout.read(4096);
        statusText = typeof output === "string"
            ? output
            : Text.utf8.decode(output, { fatal: true });
    } catch {
        statusText = "";
    } finally {
        if (process !== null) {
            await process.dispose();
        }
    }
    return statusText;
}

export { refreshStatusText, statusText };
