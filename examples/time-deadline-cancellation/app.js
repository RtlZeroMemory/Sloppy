import { File } from "sloppy/fs";
import { CancellationController, Deadline, Time } from "sloppy/time";

async function loadUsers({ signal, deadline }) {
    if (signal.aborted) {
        throw signal.reason;
    }
    const text = await File.readText("data:/users.json", { deadline });
    return JSON.parse(text);
}

const deadline = Deadline.after(5000);
const controller = new CancellationController();

const users = await Time.timeout(
    async (signal) => {
        return loadUsers({ signal, deadline });
    },
    {
        afterMs: deadline.remainingMs(),
        signal: controller.signal,
    },
);

controller.cancel("request aborted");

export default users;
