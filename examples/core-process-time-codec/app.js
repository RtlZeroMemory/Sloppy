import { Text } from "sloppy/codec";
import { Process } from "sloppy/os";
import { Deadline } from "sloppy/time";

const deadline = Deadline.after(1000);
const process = await Process.start("status-tool", ["--format=text"], {
    stdout: "pipe",
    deadline,
});
let statusText = "";

try {
    const output = await process.stdout.read(4096);
    statusText = typeof output === "string" ? output : Text.utf8.decode(output, { fatal: true });
} finally {
    await process.dispose();
}

export { statusText };
