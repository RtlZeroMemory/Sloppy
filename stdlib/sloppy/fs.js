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

function stringifyJson(value, options) {
    if (options === undefined) {
        return JSON.stringify(value);
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy File.writeJson options must be a plain object.");
    }
    if (options.atomic === true) {
        throw new Error("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: atomic filesystem writes land in CORE-FS-01.E.");
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

    writeText(path, text) {
        if (typeof text !== "string") {
            throw new TypeError("Sloppy File.writeText text must be a string.");
        }
        return nativeFsBridge("writeText").writeText(validatePath(path, "writeText"), text);
    },

    writeBytes(path, bytes) {
        return nativeFsBridge("writeBytes").writeBytes(
            validatePath(path, "writeBytes"),
            validateBytes(bytes, "writeBytes"),
        );
    },

    writeJson(path, value, options) {
        return File.writeText(path, stringifyJson(value, options));
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

    open() {
        throw new Error("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: FileHandle lands in CORE-FS-01.F.");
    },
});

const Directory = Object.freeze({
    create() {
        throw new Error("SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: Directory APIs land in CORE-FS-01.E.");
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

const FileHandle = Object.freeze({});
const FileWatcher = Object.freeze({});

export { Directory, File, FileHandle, FileWatcher, Path };
