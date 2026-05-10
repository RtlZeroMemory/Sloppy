import { Directory, File } from "sloppy/fs";
import { Process, System } from "sloppy/os";

function sanitizeFilename(value) {
    const text = String(value || "report")
        .replace(/[^A-Za-z0-9_-]/g, "_")
        .slice(0, 64);
    return text.length > 0 ? text : "report";
}

export async function main(args, ctx) {
    const name = sanitizeFilename(args[0]);
    const path = `./tmp/${name}.txt`;

    await Directory.create("./tmp", { recursive: true });
    await File.writeText(path, `kind=${ctx.kind}\nplatform=${System.platform}\n`);

    const text = await File.readText(path);
    const git = await Process.run("git", ["--version"], {
        capture: "text",
        timeoutMs: 5000,
    });

    console.log(text.trim());
    console.log(git.stdout.trim());
    return git.exitCode;
}
