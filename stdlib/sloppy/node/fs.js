import { Directory, File } from "../fs.js";

function toSloppyPath(path) {
    if (typeof path !== "string") {
        return path;
    }
    const normalized = path.replace(/\\/g, "/");
    if (
        normalized.startsWith("/") ||
        normalized.startsWith("./") ||
        normalized.startsWith("../") ||
        normalized.includes("://")
    ) {
        return normalized;
    }
    return `./${normalized}`;
}

async function readFile(path, options = undefined) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    return encoding === "utf8" || encoding === "utf-8"
        ? File.readText(toSloppyPath(path))
        : File.readBytes(toSloppyPath(path));
}

async function writeFile(path, data, options = undefined) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    if (typeof data === "string" || encoding === "utf8" || encoding === "utf-8") {
        return File.writeText(toSloppyPath(path), String(data));
    }
    return File.writeBytes(toSloppyPath(path), data);
}

async function appendFile(path, data, options = undefined) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    if (typeof data === "string" || encoding === "utf8" || encoding === "utf-8") {
        return File.appendText(toSloppyPath(path), String(data));
    }
    return File.appendBytes(toSloppyPath(path), data);
}

async function unlink(path) {
    return File.delete(toSloppyPath(path));
}

function existsSync() {
    throw new Error("SLOPPY_E_NODE_SYNC_FS_UNSUPPORTED: synchronous fs APIs are not available.");
}

const promises = Object.freeze({
    access: async (path) => {
        const stat = await File.stat(toSloppyPath(path));
        if (!stat.exists) {
            throw new Error(`SLOPPY_E_NODE_FS_ACCESS: ${path} is not accessible.`);
        }
    },
    appendFile,
    mkdir: (path, options) =>
        Directory.create(toSloppyPath(path), { recursive: Boolean(options?.recursive) }),
    readdir: async (path) => (await Directory.list(toSloppyPath(path))).map((entry) => entry.name),
    readFile,
    rm: async (path, options = undefined) => {
        const sloppyPath = toSloppyPath(path);
        const stat = await File.stat(sloppyPath);
        if (stat.kind === "directory") {
            return Directory.delete(sloppyPath, { recursive: Boolean(options?.recursive) });
        }
        return File.delete(sloppyPath);
    },
    stat: (path) => File.stat(toSloppyPath(path)),
    unlink,
    writeFile,
});

export { appendFile, existsSync, promises, readFile, unlink, writeFile };
export default { appendFile, existsSync, promises, readFile, unlink, writeFile };
