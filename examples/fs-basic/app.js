import { Directory, File } from "sloppy/fs";

await Directory.create("./tmp", { recursive: true });
await File.writeJson("./tmp/users.json", [{ id: 1, name: "Ada" }], {
    atomic: true,
    indent: 2,
});

const users = await File.readJson("./tmp/users.json");
await File.writeText("./tmp/summary.txt", `users=${users.length}`);
