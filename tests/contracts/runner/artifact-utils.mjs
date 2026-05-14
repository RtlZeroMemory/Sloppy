import crypto from "node:crypto";
import fs from "node:fs/promises";
import path from "node:path";

export function toPackagePath(value) {
    return value.replaceAll("\\", "/");
}

export function isAbsolutePackagePath(value) {
    return (
        path.posix.isAbsolute(value) ||
        path.win32.isAbsolute(value) ||
        /^[A-Za-z]:[\\/]/u.test(value) ||
        value.startsWith("\\\\")
    );
}

export function isSafeRelativePackagePath(value) {
    if (typeof value !== "string" || value.length === 0) {
        return false;
    }
    const normalized = path.posix.normalize(toPackagePath(value));
    return !isAbsolutePackagePath(value) && normalized !== ".." && !normalized.startsWith("../");
}

export function packagePath(root, relativePath) {
    return path.join(root, ...toPackagePath(relativePath).split("/"));
}

export async function exists(filePath) {
    try {
        await fs.access(filePath);
        return true;
    } catch {
        return false;
    }
}

export async function readJson(filePath) {
    const text = await fs.readFile(filePath, "utf8");
    return JSON.parse(text);
}

export async function sha256File(filePath) {
    const bytes = await fs.readFile(filePath);
    return `sha256:${crypto.createHash("sha256").update(bytes).digest("hex")}`;
}

export async function listFiles(root) {
    const files = [];
    async function visit(directory, prefix) {
        const entries = await fs.readdir(directory, { withFileTypes: true });
        for (const entry of entries.sort((left, right) => left.name.localeCompare(right.name))) {
            const childPrefix = prefix === "" ? entry.name : `${prefix}/${entry.name}`;
            const absolute = path.join(directory, entry.name);
            if (entry.isDirectory()) {
                await visit(absolute, childPrefix);
            } else if (entry.isFile()) {
                files.push(childPrefix);
            }
        }
    }
    await visit(root, "");
    return files;
}
