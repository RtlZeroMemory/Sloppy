import { Directory, File } from "../fs.js";

async function readFile(path, options = undefined) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    return encoding === "utf8" || encoding === "utf-8"
        ? File.readText(path)
        : File.readBytes(path);
}

async function writeFile(path, data, options = undefined) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    if (typeof data === "string" || encoding === "utf8" || encoding === "utf-8") {
        return File.writeText(path, String(data));
    }
    return File.writeBytes(path, data);
}

function existsSync() {
    throw new Error("SLOPPY_E_NODE_SYNC_FS_UNSUPPORTED: synchronous fs APIs are not available.");
}

const promises = Object.freeze({
    mkdir: (path, options) => Directory.create(path, { recursive: Boolean(options?.recursive) }),
    readFile,
    rm: (path) => File.delete(path),
    stat: (path) => File.stat(path),
    writeFile,
});

export { existsSync, promises, readFile, writeFile };
export default { existsSync, promises, readFile, writeFile };
