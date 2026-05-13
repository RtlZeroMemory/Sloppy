import assert from "node:assert/strict";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const modules = [
    "app.js",
    "results.js",
    "schema.js",
    "testing.js",
    "data.js",
    "codec.js",
    "crypto.js",
    "ffi.js",
    "fs.js",
    "health.js",
    "metrics.js",
    "net.js",
    "os.js",
    "time.js",
    "workers.js",
    "providers/sqlite.js",
    "request-id.js",
    "request-logging.js",
    "internal/capabilities.js",
    "internal/config.js",
    "internal/logging.js",
    "internal/modules.js",
    "internal/routes.js",
    "internal/services.js",
    "internal/shared.js",
];

const sourceBootstrapDir = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    "../../stdlib/sloppy",
);
const bootstrapBaseDir = process.env.SLOPPY_BOOTSTRAP_BUILD_DIR ?? sourceBootstrapDir;

for (const modulePath of modules) {
    const specifier = pathToFileURL(path.join(bootstrapBaseDir, modulePath)).href;
    const namespace = await import(specifier);
    assert.equal(typeof namespace, "object", `${modulePath} should import as an ESM module`);
}
