import { File } from "sloppy/fs";

function filesystemPath(value) {
    const path = value || "package.json";
    if (path.startsWith("./") || path.startsWith("../") || path.includes(":/")) {
        return path;
    }
    return `./${path}`;
}

export async function inspect(args) {
    const path = filesystemPath(args[0]);
    if (!(await File.exists(path))) {
        console.error(`not found: ${path}`);
        return 1;
    }
    const text = await File.readText(path);
    console.log(JSON.stringify({ path, bytes: text.length }, null, 2));
    return 0;
}
