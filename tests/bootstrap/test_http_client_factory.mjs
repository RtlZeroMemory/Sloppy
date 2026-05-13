import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { pathToFileURL } from "node:url";

import {
    Config,
    Http,
    HttpClientFactory,
    Results,
    schema,
    Sloppy,
    SloppyHttpClientError,
    TestHost,
    TestHttp,
} from "../../stdlib/sloppy/index.js";

const stdlibIndexUrl = pathToFileURL(path.resolve("stdlib/sloppy/index.js")).href;

async function assertRejectsCode(fn, code) {
    await assert.rejects(fn, (error) => {
        assert.equal(error.code, code);
        return true;
    });
}

class CountingAbortSignal {
    constructor() {
        this.aborted = false;
        this.reason = undefined;
        this.added = 0;
        this.removed = 0;
        this._listeners = new Set();
    }

    addEventListener(type, listener) {
        if (type !== "abort") {
            return;
        }
        this.added += 1;
        this._listeners.add(listener);
    }

    removeEventListener(type, listener) {
        if (type !== "abort") {
            return;
        }
        if (this._listeners.delete(listener)) {
            this.removed += 1;
        }
    }

    abort(reason = "cancelled") {
        this.aborted = true;
        this.reason = reason;
        for (const listener of [...this._listeners]) {
            listener();
        }
    }
}

{
    assert.equal(typeof Http.client, "function");
    assert.equal(typeof Http.typedClient, "function");
    assert.equal(typeof Http.retry.exponential, "function");
    assert.equal(typeof Http.circuitBreaker, "function");
    assert.equal(typeof Http.bulkhead, "function");
    assert.equal(typeof HttpClientFactory.create, "function");
    assert.equal(SloppyHttpClientError.name, "SloppyHttpClientError");

    assert.throws(() => Http.client("", { baseUrl: "http://api.example.test" }), /name/);
    assert.throws(() => Http.client("billing", { baseUrl: "/relative" }), /absolute/);
    assert.throws(() => Http.client("billing", { baseUrl: "http://api.example.test/#frag" }), /fragment/);
    assert.throws(() => Http.client("billing", { baseUrl: "http://api.example.test", timeoutMs: 0 }), /positive/);
    assert.throws(
        () => Http.client("billing", { baseUrl: "http://api.example.test", headers: { "bad header": "x" } }),
        /header names/,
    );

    const duplicateApp = Sloppy.create();
    duplicateApp.services.addHttpClient(Http.client("duplicate", { baseUrl: "http://api.example.test" }));
    assert.throws(
        () => duplicateApp.services.addHttpClient(Http.client("duplicate", { baseUrl: "http://other.example.test" })),
        /already registered/,
    );

    const SharedTyped = Http.typedClient("duplicate", {
        baseUrl: "http://api.example.test",
        endpoints: {
            getStatus: Http.get("/status").returns(200),
        },
    });
    duplicateApp.services.addHttpClient(SharedTyped);
    assert.equal(typeof duplicateApp.services.createScope().get(SharedTyped).getStatus, "function");
}

{
    const mock = TestHttp.mock()
        .get("/repos/RtlZeroMemory/Slop")
        .replyJson(200, { name: "Slop", stars: 42 });
    const Repo = schema.object({
        name: schema.string(),
        stars: schema.number(),
    });
    const client = mock.createClient("github");
    const repo = await client
        .get("/repos/{owner}/{repo}", {
            params: { owner: "RtlZeroMemory", repo: "Slop" },
            query: { token: "secret", include: "stats" },
        })
        .json(Repo);

    assert.deepEqual(repo, { name: "Slop", stars: 42 });
    mock.expectCalled("GET", "/repos/RtlZeroMemory/Slop").expectNoUnexpectedCalls();
    const metrics = client.metrics();
    assert.equal(metrics.counters.some((metric) => metric.name === "http.client.requests.total"), true);
    assert.equal(metrics.pool.connectionsReused, 0);
}

{
    const Invoice = schema.object({
        id: schema.string(),
        status: schema.string(),
        amount: schema.number(),
    });
    const Billing = Http.typedClient("billing", {
        baseUrl: Config.required("Billing:BaseUrl"),
        timeoutMs: 2000,
        retry: Http.retry.exponential({
            maxAttempts: 3,
            retryOnStatus: [500],
            initialDelayMs: 0,
            maxDelayMs: 0,
        }),
        circuitBreaker: Http.circuitBreaker({
            failureRatio: 0.5,
            minimumThroughput: 2,
            breakDurationMs: 10,
        }),
        bulkhead: Http.bulkhead({
            maxConcurrent: 2,
            maxQueue: 1,
            queueTimeoutMs: 10,
        }),
        endpoints: {
            getInvoice: Http.get("/invoices/{id}")
                .params(schema.object({ id: schema.string() }))
                .returns(200, Invoice),
        },
    });

    const app = Sloppy.create();
    app.services.addHttpClient(Billing);
    app.get("/invoices/{id}", async (ctx) => {
        const billing = ctx.services.get(Billing);
        const invoice = await billing.getInvoice({ id: ctx.route.id }, {
            signal: ctx.signal,
            correlationId: "req_1",
        });
        return Results.json(invoice);
    });

    const mock = TestHttp.mock()
        .get("/invoices/inv_1")
        .replyJson(500, { error: "retry" })
        .get("/invoices/inv_1")
        .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });

    const host = await TestHost.create(app, {
        config: { "Billing:BaseUrl": "http://billing.test" },
        httpClients: { billing: mock },
    });
    try {
        await (await host.get("/invoices/inv_1").expectStatus(200))
            .expectJson({ id: "inv_1", status: "paid", amount: 42 });
        mock.expectCalled("GET", "/invoices/inv_1").expectNoUnexpectedCalls();
    } finally {
        await host.close();
    }
}

{
    const Invoice = schema.object({ id: schema.string() });
    const Billing = Http.typedClient("billing", {
        baseUrl: "http://billing.test",
        endpoints: {
            getInvoice: Http.get("/invoices/{id}")
                .params(schema.object({ id: schema.string() }))
                .returns(200, Invoice),
        },
    });
    const mock = TestHttp.mock()
        .get("/invoices/inv_1")
        .replyJson(200, { id: 42 });
    const typed = Billing.__sloppyHttpClientRegistration.createTyped(mock.createClient("billing"));

    await assertRejectsCode(
        () => typed.getInvoice({ id: 123 }),
        "SLOPPY_E_HTTP_REQUEST_VALIDATION_FAILED",
    );
    await assertRejectsCode(
        () => typed.getInvoice({ id: "inv_1" }),
        "SLOPPY_E_HTTP_RESPONSE_VALIDATION_FAILED",
    );
}

{
    const mock = TestHttp.mock()
        .post("/payments")
        .replyJson(500, { failed: true })
        .post("/payments")
        .replyJson(200, { ok: true });
    const client = mock.createClient("payments", {
        retry: Http.retry.exponential({
            maxAttempts: 2,
            retryOnStatus: [500],
            initialDelayMs: 0,
            maxDelayMs: 0,
        }),
    });
    const response = await client.post("/payments").json({ ok: true });
    assert.equal(response.status, 500);
}

{
    const mock = TestHttp.mock()
        .post("/payments")
        .replyJson(500, { failed: true })
        .post("/payments")
        .replyJson(200, { ok: true });
    const client = mock.createClient("payments", {
        retry: Http.retry.exponential({
            maxAttempts: 2,
            retryOnStatus: [500],
            initialDelayMs: 0,
            maxDelayMs: 0,
            allowPostWithIdempotencyKey: true,
        }),
    });

    const response = await client
        .post("/payments", { headers: { "Idempotency-Key": "pay_1" } })
        .json({ amount: 42 });

    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), { ok: true });
    assert.equal(client.metrics().counters.some((metric) => metric.name === "http.client.retries.total"), true);
}

{
    const mock = TestHttp.mock()
        .get("/unstable")
        .replyJson(500, { failed: 1 })
        .get("/unstable")
        .replyJson(500, { failed: 2 });
    const client = mock.createClient("unstable", {
        circuitBreaker: Http.circuitBreaker({
            failureRatio: 0.5,
            minimumThroughput: 2,
            breakDurationMs: 1000,
        }),
    });

    assert.equal((await client.get("/unstable")).status, 500);
    assert.equal((await client.get("/unstable")).status, 500);
    assert.equal(client.health().status, "unhealthy");
    await assertRejectsCode(
        () => client.get("/unstable").send(),
        "SLOPPY_E_HTTP_CIRCUIT_OPEN",
    );
}

{
    const mock = TestHttp.mock()
        .get("/slow")
        .replyText(200, "ok", {}, { delayMs: 25 });
    const client = mock.createClient("bulk", {
        bulkhead: Http.bulkhead({
            maxConcurrent: 1,
            maxQueue: 0,
            queueTimeoutMs: 10,
        }),
    });

    const first = client.get("/slow").text();
    await new Promise((resolve) => setTimeout(resolve, 0));
    await assertRejectsCode(
        () => client.get("/slow").send(),
        "SLOPPY_E_HTTP_BULKHEAD_REJECTED",
    );
    assert.equal(await first, "ok");
    assert.equal(client.metrics().bulkhead.rejected, 1);
}

{
    const client = TestHttp.mock().createClient("secret");
    await assertRejectsCode(
        () => client.get("/missing", {
            headers: {
                Authorization: "Bearer SECRET",
                "X-Trace": "visible",
            },
            query: {
                token: "SECRET",
                include: "safe",
            },
        }).send(),
        "SLOPPY_E_HTTP_MOCK_UNEXPECTED_CALL",
    );
    const diagnostic = client.diagnostics().at(-1);
    assert.equal(diagnostic.fields.headers.Authorization, "[REDACTED]");
    assert.equal(diagnostic.fields.headers["X-Trace"], "visible");
    assert.match(diagnostic.fields.path, /token=%5BREDACTED%5D|token=\[REDACTED\]/);
    assert.doesNotMatch(JSON.stringify(client.diagnostics()), /Bearer SECRET/);
}

{
    const mock = TestHttp.mock()
        .get("/cached")
        .replyJson(200, { ok: true });
    const response = await mock.createClient("cached").get("/cached").send();

    assert.deepEqual(await response.json(), { ok: true });
    assert.deepEqual(await response.json(), { ok: true });
    assert.equal(await response.text(), "{\"ok\":true}");
    assert.deepEqual(Array.from(await response.bytes()), Array.from(new TextEncoder().encode("{\"ok\":true}")));
}

{
    const mock = TestHttp.mock()
        .get("/cached")
        .replyJson(200, { id: 42 });
    const response = await mock.createClient("cached-validation").get("/cached").send();
    await assertRejectsCode(
        () => response.json(schema.object({ id: schema.string() })),
        "SLOPPY_E_HTTP_RESPONSE_VALIDATION_FAILED",
    );
    assert.deepEqual(await response.json(), { id: 42 });
}

{
    const signal = new CountingAbortSignal();
    const mock = TestHttp.mock()
        .get("/retry")
        .replyJson(500, { failed: true })
        .get("/retry")
        .replyJson(200, { ok: true });
    const client = mock.createClient("retry-cleanup", {
        retry: Http.retry.fixed({
            maxAttempts: 2,
            retryOnStatus: [500],
            retryOnMethods: ["GET"],
            delayMs: 1,
        }),
    });

    assert.deepEqual(await (await client.get("/retry", { signal })).json(), { ok: true });
    assert.equal(signal.added, signal.removed);
    signal.abort();
    assert.equal(signal.added, signal.removed);
}

{
    const signal = new CountingAbortSignal();
    const mock = TestHttp.mock()
        .get("/slow")
        .replyText(200, "ok", {}, { delayMs: 10 });
    const client = mock.createClient("bulk-cleanup", {
        bulkhead: Http.bulkhead({
            maxConcurrent: 1,
            maxQueue: 1,
            queueTimeoutMs: 100,
        }),
    });

    const first = client.get("/slow").text();
    await new Promise((resolve) => setTimeout(resolve, 0));
    const second = client.get("/slow", { signal }).text();
    await first;
    assert.equal(await second, "ok");
    assert.equal(signal.added, signal.removed);
    signal.abort();
    assert.equal(signal.added, signal.removed);
}

{
    const client = TestHttp.mock()
        .get("/off")
        .replyJson(500, { failed: true })
        .createClient("quiet", {
            metrics: false,
            diagnostics: false,
        });

    assert.equal((await client.get("/off")).status, 500);
    await assertRejectsCode(() => client.get("/missing").send(), "SLOPPY_E_HTTP_MOCK_UNEXPECTED_CALL");
    assert.deepEqual(client.metrics().counters, []);
    assert.deepEqual(client.diagnostics(), []);
}

{
    const client = TestHttp.mock()
        .get("/closed")
        .replyText(200, "ok")
        .createClient("closed");
    assert.equal(await client.get("/closed").text(), "ok");
    await client.close();
    await client.close();
    assert.throws(() => client.get("/closed"), /SLOPPY_E_HTTP_CLIENT_CLOSED/);
}

assert.throws(
    () => Http.typedClient("reserved", {
        baseUrl: "http://api.example.test",
        endpoints: {
            send: Http.get("/send").returns(200),
        },
    }),
    /reserved client method name/,
);

{
    const openapi = {
        openapi: "3.0.3",
        info: { title: "Billing" },
        paths: {
            "/invoices/{id}": {
                get: {
                    operationId: "getInvoice",
                    parameters: [
                        { name: "id", in: "path", required: true, schema: { type: "string" } },
                        { name: "include", in: "query", schema: { type: "string" } },
                    ],
                    responses: {
                        200: {
                            description: "ok",
                            content: {
                                "application/json": {
                                    schema: { $ref: "#/components/schemas/Invoice" },
                                },
                            },
                        },
                        404: {
                            description: "problem",
                            content: {
                                "application/json": {
                                    schema: { $ref: "#/components/schemas/ProblemDetails" },
                                },
                            },
                        },
                    },
                },
            },
            "/invoices": {
                post: {
                    operationId: "createInvoice",
                    requestBody: {
                        content: {
                            "application/json": {
                                schema: { $ref: "#/components/schemas/CreateInvoice" },
                            },
                        },
                    },
                    responses: {
                        201: {
                            description: "created",
                            content: {
                                "application/json": {
                                    schema: { $ref: "#/components/schemas/Invoice" },
                                },
                            },
                        },
                    },
                },
            },
        },
        components: {
            schemas: {
                CreateInvoice: {
                    type: "object",
                    required: ["amount"],
                    properties: {
                        amount: { type: "number" },
                    },
                },
                Invoice: {
                    type: "object",
                    required: ["id", "amount"],
                    properties: {
                        amount: { type: "number" },
                        id: { type: "string" },
                        status: { type: "string", enum: ["paid", "open"] },
                    },
                },
                ProblemDetails: {
                    type: "object",
                    properties: {
                        title: { type: "string" },
                    },
                },
            },
        },
    };
    const generated = Http.generateClientFromOpenApi(openapi, {
        name: "billing",
        exportName: "BillingClient",
        baseUrlConfigKey: "Billing:BaseUrl",
    });
    const generatedAgain = Http.generateClientFromOpenApi(openapi, {
        name: "billing",
        exportName: "BillingClient",
        baseUrlConfigKey: "Billing:BaseUrl",
    });
    assert.equal(generated.source, generatedAgain.source);
    assert.equal(generated.warnings.length, 0);
    assert.match(generated.source, /Http\.typedClient\("billing"/);
    assert.match(generated.source, /getInvoice: Http\.get\("\/invoices\/\{id\}"\)/);
    assert.match(generated.source, /\.params\(schema\.object/);
    assert.match(generated.source, /\.query\(schema\.object/);
    assert.match(generated.source, /createInvoice: Http\.post\("\/invoices"\)/);
    assert.match(generated.source, /\.body\(CreateInvoice\)/);
    assert.match(generated.source, /\.returns\(201, Invoice\)/);

    const tempDir = await fs.mkdtemp(path.join(os.tmpdir(), "sloppy-generated-http-client-"));
    const modulePath = path.join(tempDir, "billing-client.mjs");
    const importableSource = generated.source.replace("\"sloppy\"", JSON.stringify(stdlibIndexUrl));
    await fs.writeFile(modulePath, importableSource);
    const imported = await import(pathToFileURL(modulePath).href);
    const mock = TestHttp.mock()
        .get("/invoices/inv_1")
        .replyJson(200, { id: "inv_1", amount: 42, status: "paid" });
    const typed = imported.BillingClient.__sloppyHttpClientRegistration.createTyped(mock.createClient("billing"));
    assert.deepEqual(await typed.getInvoice({ id: "inv_1", query: { include: "status" } }), {
        id: "inv_1",
        amount: 42,
        status: "paid",
    });
}

{
    const generated = Http.generateClientFromOpenApi({
        openapi: "3.0.3",
        paths: {
            "/union": {
                get: {
                    operationId: "getUnion",
                    responses: {
                        200: {
                            description: "ok",
                            content: {
                                "application/json": {
                                    schema: { oneOf: [{ type: "string" }, { type: "number" }] },
                                },
                            },
                        },
                    },
                },
            },
        },
    }, { name: "union" });
    assert.equal(generated.warnings.length, 1);
    assert.match(generated.source, /Unsupported OpenAPI construct/);
    assert.match(generated.source, /\.returns\(200\)/);
}

{
    const factory = HttpClientFactory.create();
    const github = Http.client("github", { baseUrl: "http://github.test" });
    factory.addClient(github);
    assert.equal(factory.get("github"), github);
    assert.throws(() => factory.addClient(github), /already registered/);
    await factory.dispose();
    await factory.dispose();
    assert.throws(() => factory.get("github"), /SLOPPY_E_HTTP_CLIENT_CLOSED/);
}
