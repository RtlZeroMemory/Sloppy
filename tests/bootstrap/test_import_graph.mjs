import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const sourceBootstrapDir = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "../../stdlib/sloppy",
);
const bootstrapBaseDir = process.env.SLOPPY_BOOTSTRAP_BUILD_DIR ?? sourceBootstrapDir;
const manifestPath = path.join(bootstrapBaseDir, "bootstrap.manifest.json");
const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
const manifestModules = new Set(manifest.modules);

async function listJavaScriptModules(root, relative = "") {
    const directory = path.join(root, relative);
    const entries = await readdir(directory, { withFileTypes: true });
    const modules = [];
    for (const entry of entries) {
        const entryRelative = path.join(relative, entry.name);
        if (entry.isDirectory()) {
            modules.push(...await listJavaScriptModules(root, entryRelative));
        } else if (entry.isFile() && entry.name.endsWith(".js")) {
            modules.push(`sloppy/${entryRelative.replaceAll(path.sep, "/")}`);
        }
    }
    return modules;
}

const sourceModules = await listJavaScriptModules(sourceBootstrapDir);
for (const modulePath of sourceModules.sort()) {
    assert.equal(
        manifestModules.has(modulePath),
        true,
        `${modulePath} should be listed in bootstrap.manifest.json`,
    );
}

for (const unsupported of [
    "sloppy/not-a-module.js",
    "sloppy/internal/not-a-module.js",
    "sloppy/node/not-a-builtin.js",
]) {
    assert.equal(
        manifestModules.has(unsupported),
        false,
        `${unsupported} should not be accepted as a bootstrap stdlib module`,
    );
}

const modules = manifest.modules.filter((modulePath) => {
    // Node compatibility shims are bundled for Sloppy's bootstrap loader, but
    // some intentionally re-export host-provided globals and are not standalone
    // Node ESM modules.
    return !modulePath.startsWith("sloppy/node/");
}).map((modulePath) => {
    assert.match(modulePath, /^sloppy\/.+\.js$/u, `${modulePath} should be a sloppy JavaScript module`);
    return modulePath.slice("sloppy/".length);
});

for (const modulePath of modules) {
    const specifier = pathToFileURL(path.join(bootstrapBaseDir, modulePath)).href;
    const namespace = await import(specifier);
    assert.equal(typeof namespace, "object", `${modulePath} should import as an ESM module`);
}
