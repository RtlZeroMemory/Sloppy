import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

import { Sloppy, Results } from "../../../stdlib/sloppy/index.js";
import { createTestHost } from "../../../stdlib/sloppy/testing.js";
import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { loadFixtures } from "../runner/fixture-loader.mjs";

const SUBSYSTEM = "static-files";
const TEXT_ENCODER = new TextEncoder();
const TEXT_DECODER = new TextDecoder();

function bytes(value) {
    return TEXT_ENCODER.encode(value);
}

function byteLength(value) {
    return bytes(value).byteLength;
}

function bytesEqual(actual, expected) {
    if (actual.byteLength !== expected.byteLength) {
        return false;
    }
    return actual.every((byte, index) => byte === expected[index]);
}

function header(response, name) {
    return response.headers.get(name);
}

async function responseBytes(response) {
    return new Uint8Array(await response.bytes());
}

async function responseText(response) {
    return TEXT_DECODER.decode(await responseBytes(response));
}

function responseHeaderObject(response) {
    return Object.fromEntries(Array.from(response.headers.entries()).sort(([left], [right]) => left.localeCompare(right)));
}

function comparableHeaders(response) {
    return responseHeaderObject(response);
}

function isNotServed(response) {
    return response.status === 403 || response.status === 404;
}

async function withTemporaryWorkspace(callback) {
    const previousCwd = process.cwd();
    const root = await fs.mkdtemp(path.join(os.tmpdir(), "sloppy-static-contract-"));
    try {
        process.chdir(root);
        return await callback(root);
    } finally {
        process.chdir(previousCwd);
        await fs.rm(root, { force: true, recursive: true });
    }
}

async function writeFixtureFiles(root) {
    const publicRoot = path.join(root, "public");
    await fs.mkdir(path.join(publicRoot, "nested"), { recursive: true });
    await fs.writeFile(path.join(publicRoot, "hello.txt"), "hello static\n");
    await fs.writeFile(path.join(publicRoot, "api"), "static api\n");
    await fs.writeFile(path.join(publicRoot, "index.html"), "<main>shell</main>\n");
    await fs.writeFile(path.join(publicRoot, "app.js"), "console.log('asset');\n");
    await fs.writeFile(path.join(publicRoot, "app.js.gz"), "compressed-js");
    await fs.writeFile(path.join(publicRoot, "custom.custom"), "custom\n");
    await fs.writeFile(path.join(publicRoot, ".env"), "SECRET=leaked\n");
    await fs.writeFile(path.join(publicRoot, "nested", "data.bin"), new Uint8Array([0, 255, 65, 10]));
    await fs.writeFile(path.join(publicRoot, "large.bin"), new Uint8Array(8192).fill(65));
    await fs.writeFile(path.join(root, "outside-secret.txt"), "outside secret\n");

    let symlinkSupported = false;
    try {
        await fs.symlink(path.join(root, "outside-secret.txt"), path.join(publicRoot, "linked-secret.txt"));
        symlinkSupported = true;
    } catch {
        symlinkSupported = false;
    }
    return { symlinkSupported };
}

function createContractApp() {
    const app = Sloppy.create();
    app.staticFiles("/assets", {
        root: "public",
        cacheControl: "public, max-age=60",
        contentType: { ".custom": "text/custom" },
        dotfiles: "deny",
        maxFileBytes: 20000,
        precompressed: ["gzip"],
    });
    app.staticFiles("/noindex", {
        root: "public",
        index: false,
    });
    app.staticFiles("/allow", {
        root: "public",
        dotfiles: "allow",
    });
    app.spa("/app", {
        root: "public",
        fallback: "index.html",
        cacheControl: {
            html: "no-cache",
            assets: "public, max-age=600",
        },
        precompressed: ["gzip"],
        maxFileBytes: 20000,
    });
    app.get("/assets/api", () => Results.text("api route\n"));
    app.get("/app/api/status", () => Results.json({ ok: true }));
    app.delete("/assets/empty", () => Results.noContent());
    return app;
}

async function checkPathSafety(host, collector, symlinkSupported) {
    const cases = [
        ["/assets/../outside-secret.txt", "../ traversal is rejected"],
        ["/assets/%2e%2e/outside-secret.txt", "encoded traversal is rejected"],
        ["/assets/..%5coutside-secret.txt", "backslash traversal is rejected"],
    ];
    const failures = [];
    for (const [target, label] of cases) {
        const response = await host.get(target);
        const body = await responseText(response);
        if (!isNotServed(response) || body.includes("outside secret")) {
            failures.push({ target, status: response.status, label });
        }
    }
    if (failures.length === 0) {
        collector.pass("static.path.no-traversal", "traversal request targets do not serve files outside the static root");
    } else {
        collector.fail("static.path.no-traversal", "traversal request targets served or exposed files outside the static root", {
            failures,
        });
    }

    const rootBoundaryFailures = [];
    const missing = await host.get("/assets/missing.txt");
    if (missing.status !== 404) {
        rootBoundaryFailures.push({ target: "/assets/missing.txt", status: missing.status, expected: 404 });
    }
    const dotfile = await host.get("/assets/.env");
    if (dotfile.status !== 403 || (await responseText(dotfile)).includes("SECRET")) {
        rootBoundaryFailures.push({ target: "/assets/.env", status: dotfile.status, expected: 403 });
    }
    const allowedDotfile = await host.get("/allow/.env");
    if (allowedDotfile.status !== 200 || !(await responseText(allowedDotfile)).includes("SECRET=leaked")) {
        rootBoundaryFailures.push({ target: "/allow/.env", status: allowedDotfile.status, expected: 200 });
    }
    if (symlinkSupported) {
        const linked = await host.get("/assets/linked-secret.txt");
        if (!isNotServed(linked) || (await responseText(linked)).includes("outside secret")) {
            rootBoundaryFailures.push({ target: "/assets/linked-secret.txt", status: linked.status, expected: "403 or 404" });
        }
    }
    if (rootBoundaryFailures.length === 0) {
        collector.pass("static.path.root-boundary", "static serving preserves root, missing-file, dotfile, and symlink boundaries", {
            symlinkChecked: symlinkSupported,
        });
    } else {
        collector.fail("static.path.root-boundary", "static serving violated a root boundary", {
            failures: rootBoundaryFailures,
            symlinkChecked: symlinkSupported,
        });
    }
}

async function checkRouting(host, collector) {
    const api = await host.get("/assets/api");
    const apiText = await responseText(api);
    if (api.status === 200 && apiText === "api route\n") {
        collector.pass("static.routing.api-precedence", "API routes win over matching static files and static files serve when no route matches");
    } else {
        collector.fail("static.routing.api-precedence", "static files took precedence over a configured API route", {
            status: api.status,
            body: apiText,
        });
    }

    const asset = await host.get("/assets/hello.txt");
    const index = await host.get("/assets/");
    const noIndex = await host.get("/noindex/");
    const spaApi = await host.get("/app/api/status");
    const spaFallback = await host.get("/app/dashboard");
    const spaMissingAsset = await host.get("/app/missing.js");
    if (
        asset.status === 200 &&
        (await responseText(asset)) === "hello static\n" &&
        index.status === 200 &&
        (await responseText(index)) === "<main>shell</main>\n" &&
        noIndex.status === 404 &&
        spaApi.status === 200 &&
        (await spaApi.json()).ok === true &&
        spaFallback.status === 200 &&
        (await responseText(spaFallback)) === "<main>shell</main>\n" &&
        spaMissingAsset.status === 404
    ) {
        collector.pass("static.spa-fallback.boundary", "SPA fallback, static files, missing assets, and index settings stay in their configured lanes");
    } else {
        collector.fail("static.spa-fallback.boundary", "SPA fallback or directory-index behavior crossed a configured boundary", {
            asset: asset.status,
            index: index.status,
            noIndex: noIndex.status,
            spaApi: spaApi.status,
            spaFallback: spaFallback.status,
            spaMissingAsset: spaMissingAsset.status,
        });
    }
}

async function checkHeadSemantics(host, collector) {
    const get = await host.get("/assets/hello.txt");
    const head = await host.head("/assets/hello.txt");
    const headBody = await responseBytes(head);
    if (head.status === get.status && headBody.byteLength === 0) {
        collector.pass("static.head.no-body", "HEAD returns the matching GET status without a response body");
    } else {
        collector.fail("static.head.no-body", "HEAD returned a body or mismatched status", {
            getStatus: get.status,
            headStatus: head.status,
            headLength: headBody.byteLength,
        });
    }

    const getHeaders = comparableHeaders(get);
    const headHeaders = comparableHeaders(head);
    if (JSON.stringify(getHeaders) === JSON.stringify(headHeaders)) {
        collector.pass("static.head.header-equivalence", "HEAD preserves the same headers as GET for the same static file");
    } else {
        collector.fail("static.head.header-equivalence", "HEAD headers diverged from GET headers", {
            get: getHeaders,
            head: headHeaders,
        });
    }
}

async function checkContentAndCache(host, collector) {
    const text = await host.get("/assets/hello.txt");
    const textBytes = await responseBytes(text);
    const bin = await host.get("/assets/nested/data.bin");
    const binBytes = await responseBytes(bin);
    const large = await host.get("/assets/large.bin");
    const largeBytes = await responseBytes(large);

    if (
        header(text, "content-length") === String(textBytes.byteLength) &&
        header(bin, "content-length") === String(binBytes.byteLength) &&
        header(large, "content-length") === String(largeBytes.byteLength)
    ) {
        collector.pass("static.content-length", "Content-Length matches GET body bytes, including binary and large files");
    } else {
        collector.fail("static.content-length", "Content-Length mismatched body bytes", {
            text: { header: header(text, "content-length"), bytes: textBytes.byteLength },
            bin: { header: header(bin, "content-length"), bytes: binBytes.byteLength },
            large: { header: header(large, "content-length"), bytes: largeBytes.byteLength },
        });
    }

    const custom = await host.get("/assets/custom.custom");
    if (
        header(text, "content-type")?.startsWith("text/plain") === true &&
        header(bin, "content-type") === "application/octet-stream" &&
        header(custom, "content-type") === "text/custom"
    ) {
        collector.pass("static.content-type", "content-type detection and overrides are stable");
    } else {
        collector.fail("static.content-type", "content-type detection or overrides drifted", {
            text: header(text, "content-type"),
            bin: header(bin, "content-type"),
            custom: header(custom, "content-type"),
        });
    }

    const etag = header(text, "etag");
    const notModified = await host.get("/assets/hello.txt", { headers: { "If-None-Match": etag } });
    const mismatch = await host.get("/assets/hello.txt", { headers: { "If-None-Match": "\"different\"" } });
    const lastModified = header(text, "last-modified");
    const since = await host.get("/assets/hello.txt", { headers: { "If-Modified-Since": lastModified } });
    if (
        typeof etag === "string" &&
        etag.length > 0 &&
        notModified.status === 304 &&
        (await responseBytes(notModified)).byteLength === 0 &&
        mismatch.status === 200 &&
        typeof lastModified === "string" &&
        lastModified.length > 0 &&
        since.status === 304 &&
        (await responseBytes(since)).byteLength === 0
    ) {
        collector.pass("static.etag.if-none-match", "ETag and Last-Modified validators return 304 only when the request validator matches");
    } else {
        collector.fail("static.etag.if-none-match", "static validators did not follow current cache semantics", {
            etag,
            notModified: notModified.status,
            mismatch: mismatch.status,
            lastModified,
            since: since.status,
        });
    }

    const spaHtml = await host.get("/app/dashboard");
    const spaAsset = await host.get("/app/app.js");
    if (
        header(text, "cache-control") === "public, max-age=60" &&
        header(spaHtml, "cache-control") === "no-cache" &&
        header(spaAsset, "cache-control") === "public, max-age=600"
    ) {
        collector.pass("static.cache-control", "Cache-Control follows static and SPA asset configuration");
    } else {
        collector.fail("static.cache-control", "Cache-Control did not match static or SPA configuration", {
            static: header(text, "cache-control"),
            spaHtml: header(spaHtml, "cache-control"),
            spaAsset: header(spaAsset, "cache-control"),
        });
    }

    const compressedDenied = await host.get("/assets/app.js");
    const compressedAllowed = await host.get("/assets/app.js", { headers: { "Accept-Encoding": "gzip" } });
    const range = await host.get("/assets/hello.txt", { headers: { Range: "bytes=0-4" } });
    if (
        bytesEqual(textBytes, bytes("hello static\n")) &&
        bytesEqual(binBytes, new Uint8Array([0, 255, 65, 10])) &&
        largeBytes.byteLength === 8192 &&
        header(compressedDenied, "content-encoding") === undefined &&
        header(compressedAllowed, "content-encoding") === "gzip" &&
        (await responseText(compressedAllowed)) === "compressed-js" &&
        range.status === 206 &&
        (await responseText(range)) === "hello"
    ) {
        collector.pass("static.body-bytes", "text, binary, large, precompressed, and range static file bytes are preserved", {
            precompressed: true,
            range: true,
        });
    } else {
        collector.fail("static.body-bytes", "static file body, precompressed asset, or range behavior drifted", {
            text: Array.from(textBytes),
            bin: Array.from(binBytes),
            large: largeBytes.byteLength,
            compressedDenied: header(compressedDenied, "content-encoding"),
            compressedAllowed: header(compressedAllowed, "content-encoding"),
            rangeStatus: range.status,
            rangeBody: await responseText(range),
        });
    }
}

async function checkNoBodyStatus(host, collector) {
    const response = await host.delete("/assets/empty");
    const body = await responseBytes(response);
    if (response.status === 204 && body.byteLength === 0 && header(response, "content-length") === "0") {
        collector.pass("static.status.no-body", "204 responses preserve no-body semantics in the same TestHost lane", {
            status: response.status,
        });
    } else {
        collector.fail("static.status.no-body", "204 response returned a body or wrong Content-Length", {
            status: response.status,
            length: body.byteLength,
            contentLength: header(response, "content-length"),
        });
    }
}

async function validateLiveStaticFiles() {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "testhost-static-files" });
    await withTemporaryWorkspace(async (root) => {
        const { symlinkSupported } = await writeFixtureFiles(root);
        const host = createTestHost(createContractApp());
        try {
            await checkPathSafety(host, collector, symlinkSupported);
            await checkRouting(host, collector);
            await checkHeadSemantics(host, collector);
            await checkContentAndCache(host, collector);
            await checkNoBodyStatus(host, collector);
        } finally {
            await host.close();
        }
    });
    return collector.findings;
}

function normalizeTranscriptResponse(response) {
    const headers = {};
    for (const [name, value] of Object.entries(response.headers ?? {})) {
        headers[name.toLowerCase()] = String(value);
    }
    const body = response.bodyText ?? "";
    return {
        method: response.method ?? "GET",
        target: response.target ?? "/",
        requestHeaders: Object.fromEntries(
            Object.entries(response.requestHeaders ?? {}).map(([name, value]) => [name.toLowerCase(), String(value)]),
        ),
        status: response.status,
        headers,
        body,
        bodyLength: byteLength(body),
    };
}

function validateTranscriptFixture({ fixture, config }) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    const responses = (config.responses ?? []).map(normalizeTranscriptResponse);
    for (const response of responses) {
        const targetLower = response.target.toLowerCase();
        if (
            response.status < 400 &&
            (/\/\.\.(?:\/|$)/u.test(targetLower) || /%2e%2e/u.test(targetLower) || /%5c|\\/u.test(targetLower))
        ) {
            collector.fail("static.path.no-traversal", "traversal-like request served successfully", {
                target: response.target,
                status: response.status,
            });
        }
        if (
            response.status < 400 &&
            (targetLower.includes("/../") || targetLower.includes("outside") || response.body.toLowerCase().includes("outside secret"))
        ) {
            collector.fail("static.path.root-boundary", "response exposed content outside the static root", {
                target: response.target,
                status: response.status,
            });
        }
        if (response.target === "/assets/api" && response.status === 200 && response.body !== "api route\n") {
            collector.fail("static.routing.api-precedence", "static response swallowed a configured API route");
        }
        if (response.target === "/app/api/status" && response.status === 200 && response.body !== "{\"ok\":true}") {
            collector.fail("static.spa-fallback.boundary", "SPA fallback swallowed a configured API route");
        }
        if (response.method === "HEAD" && response.bodyLength !== 0) {
            collector.fail("static.head.no-body", "HEAD response returned a body", {
                bodyLength: response.bodyLength,
            });
        }
        const contentLength = response.headers["content-length"];
        if (contentLength !== undefined && Number(contentLength) !== response.bodyLength) {
            collector.fail("static.content-length", "Content-Length does not match body bytes", {
                contentLength,
                bodyLength: response.bodyLength,
            });
        }
        if (
            response.requestHeaders["if-none-match"] !== undefined &&
            response.requestHeaders["if-none-match"] !== response.headers.etag &&
            response.status === 304
        ) {
            collector.fail("static.etag.if-none-match", "ETag mismatch returned 304");
        }
        if (/\/\.[^/]+$/u.test(response.target) && response.status < 400) {
            collector.fail("static.path.root-boundary", "dotfile was served despite the deny policy", {
                target: response.target,
                status: response.status,
            });
        }
    }
    return collector.findings;
}

function detectedErrorDetails(rawFindings) {
    return rawFindings
        .filter((finding) => finding.status === "fail" && finding.severity === "error")
        .map((finding) => ({
            invariant: finding.invariant,
            message: finding.message,
            details: finding.details,
        }));
}

function expectedFailureFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture produced expected ${invariant} finding`,
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

function failedExpectationFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.missing`,
        status: "fail",
        severity: "error",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture did not produce expected ${invariant} finding`,
        details: {
            detected,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

function unexpectedErrorFinding(fixture, invariant, expectedInvariants, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.unexpected`,
        status: "pass",
        severity: "warning",
        subsystem: SUBSYSTEM,
        invariant: `negative.unexpected.${invariant}`,
        fixture,
        message: `broken fixture produced unexpected ${invariant} finding`,
        details: {
            expectedInvariants,
            detectedFindings: detectedErrorDetails(rawFindings),
        },
    });
}

async function readFixtureConfig(root) {
    const configPath = path.join(root, "contract-fixture.json");
    return JSON.parse(await fs.readFile(configPath, "utf8"));
}

export async function runStaticFilesContract({ repoRoot, tier }) {
    const startedAt = new Date().toISOString();
    const findings = [...(await validateLiveStaticFiles())];
    const fixtures = await loadFixtures(path.join(repoRoot, "tests/contracts/static-files/fixtures"));
    for (const fixture of fixtures) {
        const config = await readFixtureConfig(fixture.root);
        const rawFindings = validateTranscriptFixture({ fixture: fixture.name, config });
        const detected = errorInvariants(rawFindings);
        if (fixture.expected === "fail") {
            const expectedInvariants = new Set(fixture.expectedInvariants);
            for (const invariant of fixture.expectedInvariants) {
                findings.push(
                    detected.includes(invariant)
                        ? expectedFailureFinding(fixture.name, invariant, detected, rawFindings)
                        : failedExpectationFinding(fixture.name, invariant, detected, rawFindings),
                );
            }
            for (const invariant of detected) {
                if (!expectedInvariants.has(invariant)) {
                    findings.push(
                        unexpectedErrorFinding(fixture.name, invariant, fixture.expectedInvariants, rawFindings),
                    );
                }
            }
        } else {
            findings.push(...rawFindings);
        }
    }

    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
