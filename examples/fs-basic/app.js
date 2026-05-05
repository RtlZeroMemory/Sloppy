import { Directory, File } from "sloppy/fs";
import { Deadline } from "sloppy/time";

await Directory.create("./tmp", { recursive: true });
await File.writeJson("./tmp/users.json", [{ id: 1, name: "Ada" }], {
    atomic: true,
    indent: 2,
});

const deadline = Deadline.after(1000);
const users = await File.readJson("./tmp/users.json", { deadline });
await File.writeText("./tmp/summary.txt", `users=${users.length}`);
