import { Directory, File } from "../fs.js";
import { Base64, Text } from "../codec.js";

function bytesFromBase64(value) {
    return Base64.decode(value);
}

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

function sealedAssetMap() {
    const map = globalThis.__sloppy_program_assets;
    return (map && typeof map === "object") ? map : null;
}

function lookupSealedAsset(path) {
    const map = sealedAssetMap();
    if (!map) {
        return undefined;
    }
    const sloppyPath = toSloppyPath(path);
    if (Object.prototype.hasOwnProperty.call(map, sloppyPath)) {
        return map[sloppyPath];
    }
    if (typeof path === "string" && Object.prototype.hasOwnProperty.call(map, path)) {
        return map[path];
    }
    const normalized = typeof path === "string"
        ? path.replace(/\\/g, "/").replace(/^(\.\/)+/, "")
        : null;
    if (normalized && Object.prototype.hasOwnProperty.call(map, normalized)) {
        return map[normalized];
    }
    return undefined;
}

function unsealedSyncError(operation, path) {
    return new Error(
        `SLOPPY_E_NODE_SYNC_FS_UNSEALED: node:fs.${operation} requires the path to be a sealed package asset; '${path}' was not found in the bundled asset map.`,
    );
}

function decodeSealedAssetText(entry) {
    if (typeof entry === "string") {
        return entry;
    }
    if (entry && typeof entry === "object" && typeof entry.text === "string") {
        return entry.text;
    }
    if (entry && typeof entry === "object" && typeof entry.base64 === "string") {
        const bytes = bytesFromBase64(entry.base64);
        return Text.utf8.decode(bytes);
    }
    return undefined;
}

function decodeSealedAssetBytes(entry) {
    if (entry instanceof Uint8Array) {
        return entry;
    }
    if (entry && typeof entry === "object" && entry.bytes instanceof Uint8Array) {
        return entry.bytes;
    }
    if (entry && typeof entry === "object" && typeof entry.base64 === "string") {
        return bytesFromBase64(entry.base64);
    }
    if (typeof entry === "string") {
        return Text.utf8.encode(entry);
    }
    return undefined;
}

function readFileSync(path, options = undefined) {
    const entry = lookupSealedAsset(path);
    if (entry === undefined) {
        throw unsealedSyncError("readFileSync", path);
    }
    const encoding = normalizeEncoding(options, "node:fs.readFileSync");
    if (encoding === "utf8") {
        const text = decodeSealedAssetText(entry);
        if (text === undefined) {
            throw unsealedSyncError("readFileSync", path);
        }
        return text;
    }
    const bytes = decodeSealedAssetBytes(entry);
    if (bytes === undefined) {
        throw unsealedSyncError("readFileSync", path);
    }
    return bytes;
}

function existsSync(path) {
    return lookupSealedAsset(path) !== undefined;
}

function statSync(path, options = undefined) {
    const entry = lookupSealedAsset(path);
    if (entry === undefined) {
        const opts = options && typeof options === "object" ? options : {};
        if (opts.throwIfNoEntry === false) {
            return undefined;
        }
        throw unsealedSyncError("statSync", path);
    }
    const bytes = decodeSealedAssetBytes(entry);
    return nodeStats({
        kind: "file",
        exists: true,
        size: bytes ? bytes.byteLength : 0,
    });
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

export { access, appendFile, copyFile, existsSync, lstat, mkdir, mkdtemp, promises, readdir, readFile, readFileSync, readlink, realpath, rename, rm, stat, statSync, symlink, unlink, writeFile };
export default { access, appendFile, copyFile, existsSync, lstat, mkdir, mkdtemp, promises, readdir, readFile, readFileSync, readlink, realpath, rename, rm, stat, statSync, symlink, unlink, writeFile };
