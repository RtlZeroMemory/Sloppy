import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const sourceBootstrapDir = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "../../stdlib/sloppy",
);
const bootstrapBaseDir = process.env.SLOPPY_BOOTSTRAP_BUILD_DIR ?? sourceBootstrapDir;
const manifestPath = path.join(bootstrapBaseDir, "bootstrap.manifest.json");
const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
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
