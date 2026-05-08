import assert from "node:assert/strict";

const modules = [
    "../../stdlib/sloppy/app.js",
    "../../stdlib/sloppy/results.js",
    "../../stdlib/sloppy/schema.js",
    "../../stdlib/sloppy/data.js",
    "../../stdlib/sloppy/codec.js",
    "../../stdlib/sloppy/crypto.js",
    "../../stdlib/sloppy/fs.js",
    "../../stdlib/sloppy/net.js",
    "../../stdlib/sloppy/os.js",
    "../../stdlib/sloppy/time.js",
    "../../stdlib/sloppy/workers.js",
    "../../stdlib/sloppy/providers/sqlite.js",
    "../../stdlib/sloppy/internal/config.js",
    "../../stdlib/sloppy/internal/logging.js",
    "../../stdlib/sloppy/internal/modules.js",
    "../../stdlib/sloppy/internal/shared.js",
];

for (const modulePath of modules) {
    const namespace = await import(modulePath);
    assert.equal(typeof namespace, "object", `${modulePath} should import as an ESM module`);
}
