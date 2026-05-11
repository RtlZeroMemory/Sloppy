function normalize(input) {
    const absolute = String(input || "").startsWith("/");
    const parts = [];
    for (const part of String(input || "").replace(/\\/g, "/").split("/")) {
        if (part === "" || part === ".") {
            continue;
        }
        if (part === "..") {
            if (parts.length > 0 && parts[parts.length - 1] !== "..") {
                parts.pop();
            } else if (!absolute) {
                parts.push("..");
            }
            continue;
        }
        parts.push(part);
    }
    const joined = parts.join("/");
    return `${absolute ? "/" : ""}${joined}` || ".";
}

function join(...parts) {
    return normalize(parts.filter((part) => part !== "").join("/"));
}

function resolve(...parts) {
    let output = "";
    for (const part of parts) {
        const text = String(part);
        output = text.startsWith("/") ? text : `${output}/${text}`;
    }
    return normalize(output || ".");
}

function basename(input, ext = "") {
    const text = normalize(input);
    const base = text.slice(text.lastIndexOf("/") + 1);
    return ext && base.endsWith(ext) ? base.slice(0, -ext.length) : base;
}

function dirname(input) {
    const text = normalize(input);
    const index = text.lastIndexOf("/");
    if (index <= 0) {
        return text.startsWith("/") ? "/" : ".";
    }
    return text.slice(0, index);
}

function extname(input) {
    const base = basename(input);
    const index = base.lastIndexOf(".");
    return index > 0 ? base.slice(index) : "";
}

function isAbsolute(input) {
    return String(input || "").startsWith("/");
}

const sep = "/";
const delimiter = ":";

const posix = Object.freeze({
    basename,
    delimiter,
    dirname,
    extname,
    isAbsolute,
    join,
    normalize,
    resolve,
    sep,
});

const win32 = posix;

export { basename, delimiter, dirname, extname, isAbsolute, join, normalize, posix, resolve, sep, win32 };
export default posix;
