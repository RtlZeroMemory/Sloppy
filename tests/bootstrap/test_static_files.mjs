import assert from "node:assert/strict";
import { mkdtemp, mkdir, writeFile, rm, symlink } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { gzipSync, gunzipSync, brotliCompressSync, brotliDecompressSync } from "node:zlib";

import { Results, Sloppy, TestHost } from "../../stdlib/sloppy/index.js";

const previousCwd = process.cwd();
const project = await mkdtemp(join(tmpdir(), "sloppy-static-files-"));

try {
    await mkdir(join(project, "public", "css"), { recursive: true });
    await mkdir(join(project, "dist", "assets"), { recursive: true });
    await writeFile(join(project, "public", "app.js"), "console.log('static');\n");
    await writeFile(join(project, "public", "css", "site.css"), "body { color: #123456; }\n");
    await writeFile(join(project, "public", ".secret"), "hidden\n");
    await writeFile(join(project, "public", "same-size.txt"), "aaaa\n");
    await writeFile(join(project, "public", "tiny.js"), "ok\n");
    await writeFile(join(project, "public", "tiny.js.gz"), Buffer.alloc(16, 31));
    await writeFile(join(project, "public", "app.js.gz"), gzipSync("console.log('static');\n"));
    await writeFile(join(project, "public", "app.js.br"), brotliCompressSync(Buffer.from("console.log('static');\n")));
    await writeFile(join(project, "dist", "index.html"), "<!doctype html><main>SPA</main>");
    await writeFile(join(project, "dist", "assets", "app.js"), "globalThis.spa = true;\n");
    await writeFile(join(project, "outside-secret.txt"), "outside\n");
    try {
        await symlink(join(project, "outside-secret.txt"), join(project, "public", "linked-secret.txt"));
    } catch {
        // Windows without developer-mode symlink privileges still exercises traversal below.
    }

    process.chdir(project);

    const app = Sloppy.create();
    app.mapGet("/api/users", () => Results.json({ ok: true }));
    app.staticFiles("/assets", {
        root: "public",
        cacheControl: "public, max-age=31536000, immutable",
        precompressed: true,
        dotfiles: "deny",
    });
    app.staticFiles("/allow", {
        root: "public",
        dotfiles: "allow",
    });
    app.staticFiles("/ignore", {
        root: "public",
        dotfiles: "ignore",
    });
    app.staticFiles("/limited", {
        root: "public",
        precompressed: ["gzip"],
        maxFileBytes: 4,
    });
    app.spa("/", {
        root: "dist",
        fallback: "index.html",
        cacheControl: {
            assets: "public, max-age=31536000, immutable",
            html: "no-cache",
        },
        precompressed: true,
    });
    const limitedSpa = Sloppy.create();
    limitedSpa.spa("/", {
        root: "dist",
        fallback: "index.html",
        maxFileBytes: 4,
    });

    const host = await TestHost.create(app);

    const js = await host.get("/assets/app.js");
    js.expectStatus(200)
        .expectHeader("content-type", /javascript/u)
        .expectHeader("cache-control", "public, max-age=31536000, immutable")
        .expectHeader("etag", /W\//u)
        .expectHeader("last-modified", /GMT/u)
        .expectHeader("accept-ranges", "bytes")
        .expectHeader("x-content-type-options", "nosniff")
        .expectText("console.log('static');\n");

    await host.head("/assets/app.js")
        .then((response) => response.expectStatus(200).expectNoBody().expectHeader("content-length", String(js.bytes().byteLength)));

    await host.get("/assets/app.js", { headers: { "if-none-match": js.headers.get("etag") } })
        .then((response) => response.expectStatus(304).expectNoBody());

    const sameSize = await host.get("/assets/same-size.txt");
    await writeFile(join(project, "public", "same-size.txt"), "bbbb\n");
    await host.get("/assets/same-size.txt", { headers: { "if-none-match": sameSize.headers.get("etag") } })
        .then((response) => response.expectStatus(200).expectText("bbbb\n"));
    await host.get("/assets/same-size.txt")
        .then((response) => assert.notEqual(response.headers.get("etag"), sameSize.headers.get("etag")));

    await host.get("/assets/app.js", { headers: { range: "bytes=0-6" } })
        .then((response) => response.expectStatus(206).expectHeader("content-range", `bytes 0-6/${js.bytes().byteLength}`).expectText("console"));

    await host.get("/assets/app.js", { headers: { range: "bytes=999-1000" } })
        .then((response) => response.expectStatus(416).expectNoBody().expectHeader("content-range", `bytes */${js.bytes().byteLength}`));

    await host.get("/assets/app.js", { headers: { "accept-encoding": "gzip" } })
        .then((response) => {
            response.expectStatus(200).expectHeader("content-encoding", "gzip").expectHeader("vary", "Accept-Encoding");
            assert.equal(gunzipSync(response.bytes()).toString("utf8"), "console.log('static');\n");
        });

    await host.get("/assets/app.js", { headers: { "accept-encoding": "gzip, br" } })
        .then((response) => {
            response.expectStatus(200).expectHeader("content-encoding", "br");
            assert.equal(brotliDecompressSync(response.bytes()).toString("utf8"), "console.log('static');\n");
        });

    await host.get("/assets/app.js", { headers: { "accept-encoding": "br;q=0, gzip;q=1" } })
        .then((response) => response.expectStatus(200).expectHeader("content-encoding", "gzip"));

    await host.get("/assets/app.js", { headers: { "accept-encoding": "br;q=0.2, gzip;q=0.9" } })
        .then((response) => response.expectStatus(200).expectHeader("content-encoding", "gzip"));

    await host.get("/assets/app.js", { headers: { "accept-encoding": "gzip;q=0, br;q=0" } })
        .then((response) => response.expectStatus(200).expectHeader("accept-ranges", "bytes").expectText("console.log('static');\n"));

    await host.get("/assets/app.js", { headers: { "accept-encoding": "br", range: "bytes=0-6" } })
        .then((response) => response.expectStatus(200).expectHeader("content-encoding", "br").expectHeader("accept-ranges", "none"));

    await host.get("/assets/.secret")
        .then((response) => response.expectStatus(403));

    await host.get("/ignore/.secret")
        .then((response) => response.expectStatus(404));

    await host.get("/allow/.secret")
        .then((response) => response.expectStatus(200).expectText("hidden\n"));

    await host.get("/limited/tiny.js", { headers: { "accept-encoding": "gzip" } })
        .then((response) => response.expectStatus(413));

    await host.get("/assets/%2e%2e/secret.txt")
        .then((response) => response.expectStatus(403));

    await host.get("/assets/linked-secret.txt")
        .then((response) => {
            if (response.status !== 404) {
                response.expectStatus(403);
            }
        });

    await host.get("/dashboard")
        .then((response) => response.expectStatus(200).expectHeader("cache-control", "no-cache").expectText("<!doctype html><main>SPA</main>"));

    await host.get("/a/b/c/d/e/f/g/h/i/j")
        .then((response) => response.expectStatus(200).expectText("<!doctype html><main>SPA</main>"));

    await host.get("/dist-missing.js")
        .then((response) => response.expectStatus(404));

    await host.get("/api/users")
        .then((response) => response.expectStatus(200).expectJson({ ok: true }));

    await host.post("/assets/app.js")
        .then((response) => response.expectStatus(404));

    const limitedSpaHost = await TestHost.create(limitedSpa);
    await limitedSpaHost.get("/dashboard")
        .then((response) => response.expectStatus(413));

    assert.throws(() => Sloppy.create().spa("/", { root: "dist", fallback: "index.html", maxFileBytes: 0 }), /maxFileBytes/u);
    assert.throws(() => Sloppy.create().spa("/", { root: "dist", fallback: "index.html", maxFileBytes: -1 }), /maxFileBytes/u);
    assert.throws(() => Sloppy.create().spa("/", { root: "dist", fallback: "index.html", maxFileBytes: 1.5 }), /maxFileBytes/u);
    assert.throws(() => Sloppy.create().spa("/", { root: "dist", fallback: "index.html", maxFileBytes: 129 * 1024 * 1024 }), /maxFileBytes/u);

    assert.deepEqual(app.__getPlanContributions().staticAssets.map((entry) => entry.kind), ["static", "static", "static", "static", "spa"]);
} finally {
    process.chdir(previousCwd);
    await rm(project, { recursive: true, force: true });
}
