import { Directory, File } from "sloppy/fs";

await Directory.create("data:/exports", { recursive: true });
await File.writeText("data:/exports/report.txt", "ready", { atomic: true });
