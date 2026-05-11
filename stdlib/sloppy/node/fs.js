import { Directory, File } from "../fs.js";

function toSloppyPath(path) {
    if (typeof path !== "string") {
        return path;
    }
    const normalized = path.replace(/\\/g, "/");
    if (
        normalized.startsWith("/") ||
        /^[A-Za-z]:\//.test(normalized) ||
        normalized.startsWith("./") ||
        normalized.startsWith("../") ||
        normalized.includes("://")
    ) {
        return normalized;
    }
    return `./${normalized}`;
}

function normalizeEncoding(options, operation) {
    const encoding = typeof options === "string" ? options : options?.encoding;
    if (encoding === undefined || encoding === null) {
        return undefined;
    }
    const normalized = String(encoding).toLowerCase();
    if (normalized === "utf8" || normalized === "utf-8") {
        return "utf8";
    }
    throw new TypeError(`SLOPPY_E_NODE_FS_UNSUPPORTED_ENCODING: ${operation} only supports utf8 text encoding.`);
}

function nodeStats(stat) {
    return Object.freeze({
        ...stat,
        isFile() {
            return stat.kind === "file" || (stat.exists === true && stat.kind !== "directory");
        },
        isDirectory() {
            return stat.kind === "directory";
        },
    });
}

async function readFilePromise(path, options = undefined) {
    const encoding = normalizeEncoding(options, "node:fs.readFile");
    return encoding === "utf8"
        ? File.readText(toSloppyPath(path))
        : File.readBytes(toSloppyPath(path));
}

async function writeFilePromise(path, data, options = undefined) {
    const encoding = normalizeEncoding(options, "node:fs.writeFile");
    if (typeof data === "string" || encoding === "utf8") {
        return File.writeText(toSloppyPath(path), String(data));
    }
    return File.writeBytes(toSloppyPath(path), data);
}

async function appendFilePromise(path, data, options = undefined) {
    const encoding = normalizeEncoding(options, "node:fs.appendFile");
    if (typeof data === "string" || encoding === "utf8") {
        return File.appendText(toSloppyPath(path), String(data));
    }
    return File.appendBytes(toSloppyPath(path), data);
}

async function unlinkPromise(path) {
    return File.delete(toSloppyPath(path));
}

function callbackify(method, name) {
    return (...args) => {
        const callback = args.pop();
        if (typeof callback !== "function") {
            throw new TypeError(`node:fs.${name} requires a callback.`);
        }
        method(...args).then(
            (value) => callback(null, value),
            (error) => callback(error),
        );
    };
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
    appendFile: appendFilePromise,
    mkdir: (path, options) =>
        Directory.create(toSloppyPath(path), { recursive: Boolean(options?.recursive) }),
    readdir: async (path) => (await Directory.list(toSloppyPath(path))).map((entry) => entry.name),
    readFile: readFilePromise,
    rm: async (path, options = undefined) => {
        const sloppyPath = toSloppyPath(path);
        const stat = await File.stat(sloppyPath);
        if (stat.kind === "directory") {
            return Directory.delete(sloppyPath, { recursive: Boolean(options?.recursive) });
        }
        return File.delete(sloppyPath);
    },
    stat: async (path) => nodeStats(await File.stat(toSloppyPath(path))),
    unlink: unlinkPromise,
    writeFile: writeFilePromise,
});

const access = callbackify(promises.access, "access");
const appendFile = callbackify(appendFilePromise, "appendFile");
const mkdir = callbackify(promises.mkdir, "mkdir");
const readdir = callbackify(promises.readdir, "readdir");
const readFile = callbackify(readFilePromise, "readFile");
const rm = callbackify(promises.rm, "rm");
const stat = callbackify(promises.stat, "stat");
const unlink = callbackify(unlinkPromise, "unlink");
const writeFile = callbackify(writeFilePromise, "writeFile");

export { access, appendFile, existsSync, mkdir, promises, readdir, readFile, rm, stat, unlink, writeFile };
export default { access, appendFile, existsSync, mkdir, promises, readdir, readFile, rm, stat, unlink, writeFile };
