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

function recursiveOption(options, operation) {
    const recursive = options?.recursive;
    if (recursive === undefined) {
        return false;
    }
    if (typeof recursive !== "boolean") {
        throw new TypeError(`${operation} recursive option must be a boolean.`);
    }
    return recursive;
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
        isSymbolicLink() {
            return stat.kind === "symlink" || stat.symlink === true;
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
    normalizeEncoding(options, "node:fs.writeFile");
    if (typeof data === "string") {
        return File.writeText(toSloppyPath(path), data);
    }
    return File.writeBytes(toSloppyPath(path), data);
}

async function appendFilePromise(path, data, options = undefined) {
    normalizeEncoding(options, "node:fs.appendFile");
    if (typeof data === "string") {
        return File.appendText(toSloppyPath(path), data);
    }
    return File.appendBytes(toSloppyPath(path), data);
}

async function unlinkPromise(path) {
    return File.delete(toSloppyPath(path));
}

async function copyFilePromise(fromPath, toPath) {
    return File.copy(toSloppyPath(fromPath), toSloppyPath(toPath));
}

async function renamePromise(fromPath, toPath) {
    return File.move(toSloppyPath(fromPath), toSloppyPath(toPath));
}

async function readlinkPromise(path) {
    return File.readLink(toSloppyPath(path));
}

async function symlinkPromise(targetPath, linkPath, type = undefined) {
    return File.createSymlink(toSloppyPath(targetPath), toSloppyPath(linkPath), {
        directory: type === "dir" || type === "junction",
    });
}

function callbackify(method, name) {
    return (...args) => {
        const callback = args.pop();
        if (typeof callback !== "function") {
            throw new TypeError(`node:fs.${name} requires a callback.`);
        }
        Promise.resolve()
            .then(() => method(...args))
            .then(
                (value) => callback(null, value),
                (error) => callback(error),
            );
    };
}

function existsSync() {
    throw new Error("SLOPPY_E_NODE_SYNC_FS_UNSUPPORTED: synchronous fs APIs are not available.");
}

async function unsupportedPromise(name) {
    throw new Error(`SLOPPY_E_NODE_FS_UNSUPPORTED: node:fs.promises.${name} is not implemented by Sloppy's Node compatibility shim.`);
}

const promises = Object.freeze({
    access: async (path) => {
        const stat = await File.stat(toSloppyPath(path));
        if (!stat.exists) {
            throw new Error("SLOPPY_E_NODE_FS_ACCESS: path is not accessible.");
        }
    },
    appendFile: appendFilePromise,
    copyFile: copyFilePromise,
    lstat: () => unsupportedPromise("lstat"),
    mkdir: (path, options) =>
        Directory.create(toSloppyPath(path), { recursive: recursiveOption(options, "node:fs.mkdir") }),
    mkdtemp: () => unsupportedPromise("mkdtemp"),
    readdir: async (path) => (await Directory.list(toSloppyPath(path))).map((entry) => entry.name),
    readFile: readFilePromise,
    readlink: readlinkPromise,
    realpath: () => unsupportedPromise("realpath"),
    rename: renamePromise,
    rm: async (path, options = undefined) => {
        const sloppyPath = toSloppyPath(path);
        const stat = await File.stat(sloppyPath);
        if (stat.kind === "directory") {
            return Directory.delete(sloppyPath, { recursive: recursiveOption(options, "node:fs.rm") });
        }
        return File.delete(sloppyPath);
    },
    stat: async (path) => nodeStats(await File.stat(toSloppyPath(path))),
    symlink: symlinkPromise,
    unlink: unlinkPromise,
    writeFile: writeFilePromise,
});

const access = callbackify(promises.access, "access");
const appendFile = callbackify(appendFilePromise, "appendFile");
const copyFile = callbackify(copyFilePromise, "copyFile");
const lstat = callbackify(promises.lstat, "lstat");
const mkdir = callbackify(promises.mkdir, "mkdir");
const mkdtemp = callbackify(promises.mkdtemp, "mkdtemp");
const readdir = callbackify(promises.readdir, "readdir");
const readFile = callbackify(readFilePromise, "readFile");
const readlink = callbackify(readlinkPromise, "readlink");
const realpath = callbackify(promises.realpath, "realpath");
const rename = callbackify(renamePromise, "rename");
const rm = callbackify(promises.rm, "rm");
const stat = callbackify(promises.stat, "stat");
const symlink = callbackify(symlinkPromise, "symlink");
const unlink = callbackify(unlinkPromise, "unlink");
const writeFile = callbackify(writeFilePromise, "writeFile");

export { access, appendFile, copyFile, existsSync, lstat, mkdir, mkdtemp, promises, readdir, readFile, readlink, realpath, rename, rm, stat, symlink, unlink, writeFile };
export default { access, appendFile, copyFile, existsSync, lstat, mkdir, mkdtemp, promises, readdir, readFile, readlink, realpath, rename, rm, stat, symlink, unlink, writeFile };
