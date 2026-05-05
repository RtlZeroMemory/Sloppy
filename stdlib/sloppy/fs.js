function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function nativeFsBridge(operation) {
    const bridge = globalThis.__sloppy?.fs ?? null;

    if (bridge === null) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.fs is inactive or unavailable

Feature:
  stdlib.fs

Operation:
  ${operation}

Reason:
  The active Sloppy Plan did not enable the __sloppy.fs V8 intrinsic namespace.`);
    }

    return bridge;
}

function validatePath(path, operation) {
    if (typeof path !== "string" || path.length === 0) {
        throw new TypeError(`Sloppy File.${operation} path must be a non-empty string.`);
    }
    return path;
}

function validateBytes(value, operation) {
    if (!(value instanceof Uint8Array)) {
        throw new TypeError(`Sloppy File.${operation} bytes must be a Uint8Array.`);
    }
    return value;
}

function validateCopyMoveOptions(options) {
    if (options === undefined) {
        return Object.freeze({ overwrite: false });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy File copy/move options must be a plain object.");
    }
    const overwrite = options.overwrite ?? false;
    if (typeof overwrite !== "boolean") {
        throw new TypeError("Sloppy File overwrite option must be boolean.");
    }
    return Object.freeze({ overwrite });
}

function validateRecursiveOptions(options) {
    if (options === undefined) {
        return Object.freeze({ recursive: false });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Directory options must be a plain object.");
    }
    const recursive = options.recursive ?? false;
    if (typeof recursive !== "boolean") {
        throw new TypeError("Sloppy Directory recursive option must be boolean.");
    }
    return Object.freeze({ recursive });
}

function validateOpenOptions(options) {
    if (options === undefined) {
        return Object.freeze({ access: "read", create: false });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy File.open options must be a plain object.");
    }
    const access = options.access ?? "read";
    const create = options.create ?? access !== "read";
    if (!["read", "write", "readwrite", "append"].includes(access)) {
        throw new TypeError("Sloppy File.open access must be read, write, readwrite, or append.");
    }
    if (typeof create !== "boolean") {
        throw new TypeError("Sloppy File.open create option must be boolean.");
    }
    return Object.freeze({ access, create });
}

function validateWatchOptions(options, directory) {
    if (options === undefined) {
        return Object.freeze({ recursive: false, queueCapacity: 16, snapshotCapacity: directory ? 128 : 1 });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy filesystem watch options must be a plain object.");
    }
    const recursive = options.recursive ?? false;
    const queueCapacity = options.queueCapacity ?? 16;
    const snapshotCapacity = options.snapshotCapacity ?? (directory ? 128 : 1);
    if (typeof recursive !== "boolean") {
        throw new TypeError("Sloppy filesystem watch recursive option must be boolean.");
    }
    if (!Number.isInteger(queueCapacity) || queueCapacity < 1 || queueCapacity > 256) {
        throw new TypeError("Sloppy filesystem watch queueCapacity must be 1..256.");
    }
    if (!Number.isInteger(snapshotCapacity) || snapshotCapacity < 1 || snapshotCapacity > 1024) {
        throw new TypeError("Sloppy filesystem watch snapshotCapacity must be 1..1024.");
    }
    return Object.freeze({ recursive, queueCapacity, snapshotCapacity });
}

function stringifyJson(value, options) {
    if (options === undefined) {
        return JSON.stringify(value);
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy File.writeJson options must be a plain object.");
    }
    const indent = options.indent ?? undefined;
    if (
        indent !== undefined
        && (!Number.isInteger(indent) || indent < 0 || indent > 10)
    ) {
        throw new TypeError("Sloppy File.writeJson indent must be an integer from 0 to 10.");
    }
    return JSON.stringify(value, null, indent);
}

function shouldAtomic(options) {
    if (options === undefined) {
        return false;
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy File write options must be a plain object.");
    }
    const atomic = options.atomic ?? false;
    if (typeof atomic !== "boolean") {
        throw new TypeError("Sloppy File atomic option must be boolean.");
    }
    return atomic;
}

const File = Object.freeze({
    readText(path) {
        return nativeFsBridge("readText").readText(validatePath(path, "readText"));
    },

    readBytes(path) {
        return nativeFsBridge("readBytes").readBytes(validatePath(path, "readBytes"));
    },

    async readJson(path) {
        return JSON.parse(await File.readText(path));
    },

    writeText(path, text, options) {
        if (typeof text !== "string") {
            throw new TypeError("Sloppy File.writeText text must be a string.");
        }
        if (shouldAtomic(options)) {
            return nativeFsBridge("atomicWriteText").atomicWriteText(validatePath(path, "writeText"), text);
        }
        return nativeFsBridge("writeText").writeText(validatePath(path, "writeText"), text);
    },

    writeBytes(path, bytes, options) {
        const checked = validateBytes(bytes, "writeBytes");
        if (shouldAtomic(options)) {
            return nativeFsBridge("atomicWriteBytes").atomicWriteBytes(validatePath(path, "writeBytes"), checked);
        }
        return nativeFsBridge("writeBytes").writeBytes(
            validatePath(path, "writeBytes"),
            checked,
        );
    },

    writeJson(path, value, options) {
        return File.writeText(path, stringifyJson(value, options), options);
    },

    appendText(path, text) {
        if (typeof text !== "string") {
            throw new TypeError("Sloppy File.appendText text must be a string.");
        }
        return nativeFsBridge("appendText").appendText(validatePath(path, "appendText"), text);
    },

    appendBytes(path, bytes) {
        return nativeFsBridge("appendBytes").appendBytes(
            validatePath(path, "appendBytes"),
            validateBytes(bytes, "appendBytes"),
        );
    },

    exists(path) {
        return nativeFsBridge("exists").exists(validatePath(path, "exists"));
    },

    stat(path) {
        return nativeFsBridge("stat").stat(validatePath(path, "stat"));
    },

    copy(fromPath, toPath, options) {
        return nativeFsBridge("copy").copy(
            validatePath(fromPath, "copy"),
            validatePath(toPath, "copy"),
            validateCopyMoveOptions(options),
        );
    },

    move(fromPath, toPath, options) {
        return nativeFsBridge("move").move(
            validatePath(fromPath, "move"),
            validatePath(toPath, "move"),
            validateCopyMoveOptions(options),
        );
    },

    delete(path) {
        return nativeFsBridge("delete").delete(validatePath(path, "delete"));
    },

    async open(path, options) {
        const checked = validateOpenOptions(options);
        const id = await nativeFsBridge("openHandle").openHandle(
            validatePath(path, "open"),
            checked.access,
            checked.create,
        );
        return new FileHandle(id);
    },

    async watch(path, options) {
        const checked = validateWatchOptions(options, false);
        const id = await nativeFsBridge("watch").watch(validatePath(path, "watch"), false, checked);
        return new FileWatcher(id);
    },

    createSymlink(targetPath, linkPath, options) {
        const directory = options?.directory ?? false;
        if (typeof directory !== "boolean") {
            throw new TypeError("Sloppy File.createSymlink directory option must be boolean.");
        }
        return nativeFsBridge("symlink").symlink(
            validatePath(targetPath, "createSymlink"),
            validatePath(linkPath, "createSymlink"),
            directory,
        );
    },

    readLink(path) {
        return nativeFsBridge("readLink").readLink(validatePath(path, "readLink"));
    },

    createTemp(directory, options) {
        const prefix = options?.prefix ?? "sloppy-";
        if (typeof prefix !== "string" || prefix.length === 0) {
            throw new TypeError("Sloppy File.createTemp prefix must be a non-empty string.");
        }
        return nativeFsBridge("tempFile").tempFile(validatePath(directory, "createTemp"), prefix);
    },
});

async function isSymlink(path) {
    try {
        await File.readLink(path);
        return true;
    }
    catch {
        return false;
    }
}

const Directory = Object.freeze({
    create(path, options) {
        const checked = validateRecursiveOptions(options);
        return nativeFsBridge("directoryCreate").directoryCreate(
            validatePath(path, "create"),
            checked.recursive,
        );
    },

    list(path) {
        return nativeFsBridge("directoryList").directoryList(validatePath(path, "list"));
    },

    async *walk(path, options) {
        const followSymlinks = options?.followSymlinks ?? false;
        if (typeof followSymlinks !== "boolean") {
            throw new TypeError("Sloppy Directory.walk followSymlinks option must be boolean.");
        }
        for (const entry of await Directory.list(path)) {
            yield entry;
            if (entry.kind === "directory") {
                const child = `${path.replace(/[\\/]$/, "")}/${entry.name}`;
                if (!followSymlinks && await isSymlink(child)) {
                    continue;
                }
                for await (const nested of Directory.walk(child, { followSymlinks })) {
                    yield { ...nested, name: `${entry.name}/${nested.name}` };
                }
            }
        }
    },

    delete(path, options) {
        const checked = validateRecursiveOptions(options);
        return nativeFsBridge("directoryDelete").directoryDelete(
            validatePath(path, "delete"),
            checked.recursive,
        );
    },

    async exists(path) {
        const stat = await File.stat(path);
        return stat.exists && stat.kind === "directory";
    },

    createTemp(directory, options) {
        const prefix = options?.prefix ?? "sloppy-";
        if (typeof prefix !== "string" || prefix.length === 0) {
            throw new TypeError("Sloppy Directory.createTemp prefix must be a non-empty string.");
        }
        return nativeFsBridge("tempDirectory").tempDirectory(
            validatePath(directory, "createTemp"),
            prefix,
        );
    },

    async watch(path, options) {
        const checked = validateWatchOptions(options, true);
        const id = await nativeFsBridge("watch").watch(validatePath(path, "watch"), true, checked);
        return new FileWatcher(id);
    },
});

const Path = Object.freeze({
    classify(path) {
        validatePath(path, "classify");
        if (/^\.[\\/]/.test(path)) {
            return "project-relative";
        }
        if (/^(?:[A-Za-z]:[\\/]|[\\/])/.test(path)) {
            return "absolute";
        }
        if (/^[A-Za-z][A-Za-z0-9_.-]*:[\\/]/.test(path)) {
            return "named-root";
        }
        return "invalid";
    },
});

class FileHandle {
    constructor(id) {
        this._id = Object.freeze({ slot: id.slot, generation: id.generation });
    }

    readBytes(maxBytes = 64 * 1024) {
        if (!Number.isInteger(maxBytes) || maxBytes <= 0 || maxBytes > 1024 * 1024) {
            throw new TypeError("Sloppy FileHandle.readBytes maxBytes must be 1..1048576.");
        }
        return nativeFsBridge("handleRead").handleRead(this._id, maxBytes);
    }

    async readText(maxBytes) {
        const bytes = await this.readBytes(maxBytes);
        return new TextDecoder().decode(bytes);
    }

    writeBytes(bytes) {
        return nativeFsBridge("handleWriteBytes").handleWriteBytes(
            this._id,
            validateBytes(bytes, "writeBytes"),
        );
    }

    writeText(text) {
        if (typeof text !== "string") {
            throw new TypeError("Sloppy FileHandle.writeText text must be a string.");
        }
        return nativeFsBridge("handleWriteText").handleWriteText(this._id, text);
    }

    seek(offset, origin = "start") {
        if (!Number.isInteger(offset) || !["start", "current", "end"].includes(origin)) {
            throw new TypeError("Sloppy FileHandle.seek requires an integer offset and valid origin.");
        }
        return nativeFsBridge("handleSeek").handleSeek(this._id, offset, origin);
    }

    truncate(size) {
        if (!Number.isInteger(size) || size < 0) {
            throw new TypeError("Sloppy FileHandle.truncate size must be a non-negative integer.");
        }
        return nativeFsBridge("handleTruncate").handleTruncate(this._id, size);
    }

    flush() {
        return nativeFsBridge("handleFlush").handleFlush(this._id);
    }

    sync() {
        return nativeFsBridge("handleSync").handleSync(this._id);
    }

    close() {
        return nativeFsBridge("handleClose").handleClose(this._id);
    }

    async *readChunks(options) {
        const chunkSize = options?.chunkSize ?? 64 * 1024;
        for (;;) {
            const chunk = await this.readBytes(chunkSize);
            if (chunk.byteLength === 0) {
                return;
            }
            yield chunk;
        }
    }

    async *readLines(options) {
        const decoder = new TextDecoder();
        const newline = options?.newline ?? "\n";
        const maxLineLength = options?.maxLineLength ?? 1024 * 1024;
        let buffered = "";
        if (typeof newline !== "string" || newline.length === 0) {
            throw new TypeError(
                "Sloppy FileHandle.readLines newline must be a non-empty string.",
            );
        }
        for await (const chunk of this.readChunks(options)) {
            buffered += decoder.decode(chunk, { stream: true });
            if (buffered.length > maxLineLength) {
                throw new Error("SLOPPY_E_LIMIT_EXCEEDED: filesystem line exceeds maxLineLength.");
            }
            let index = buffered.indexOf(newline);
            while (index !== -1) {
                yield buffered.slice(0, index).replace(/\r$/, "");
                buffered = buffered.slice(index + newline.length);
                index = buffered.indexOf(newline);
            }
        }
        buffered += decoder.decode();
        if (buffered.length !== 0) {
            yield buffered;
        }
    }
}

class FileWatcher {
    constructor(id) {
        this._id = Object.freeze({ slot: id.slot, generation: id.generation });
        this._closed = false;
    }

    async nextEvent(options) {
        if (this._closed) {
            return null;
        }
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy FileWatcher.nextEvent options must be a plain object.");
        }
        return nativeFsBridge("watchNext").watchNext(this._id);
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await nativeFsBridge("watchClose").watchClose(this._id);
    }

    [Symbol.asyncIterator]() {
        return {
            next: async () => {
                while (!this._closed) {
                    const event = await this.nextEvent();
                    if (event !== null) {
                        return { done: false, value: event };
                    }
                }
                return { done: true, value: undefined };
            },
            return: async () => {
                await this.close();
                return { done: true, value: undefined };
            },
        };
    }
}

export { Directory, File, FileHandle, FileWatcher, Path };
