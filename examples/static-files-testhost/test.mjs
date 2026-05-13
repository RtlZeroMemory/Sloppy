import { strict as assert } from "node:assert";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { gunzipSync } from "node:zlib";
import { Sloppy, Results, TestHost } from "../../stdlib/sloppy/index.js";

const app = Sloppy.create();

app.get("/api/status", () => Results.json({ ok: true }));
app.staticFiles("/assets", {
    root: "public",
    dotfiles: "deny",
    precompressed: true,
});

const host = await TestHost.create(app);
const previousCwd = process.cwd();

try {
    process.chdir(dirname(fileURLToPath(import.meta.url)));

    const status = await host.get("/api/status");
    status.expectStatus(200).expectJson({ ok: true });

    const response = await host
        .get("/assets/app.js", { headers: { "accept-encoding": "gzip" } });
    response.expectStatus(200).expectHeader("content-encoding", "gzip");

    assert.equal(gunzipSync(response.bytes()).toString("utf8"), "globalThis.sloppyTestHost = true;\n");

    const head = await host.head("/assets/app.js");
    head.expectStatus(200).expectNoBody();
    const traversal = await host.get("/assets/%2e%2e/secret.txt");
    traversal.expectStatus(403);
} finally {
    process.chdir(previousCwd);
    await host.close();
}
