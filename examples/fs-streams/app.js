import { File } from "sloppy/fs";

const file = await File.open("data:/large.log", { access: "read" });
try {
    for await (const line of file.readLines()) {
        await File.appendText("./tmp/filtered.log", `${line}\n`);
    }
} finally {
    await file.close();
}
