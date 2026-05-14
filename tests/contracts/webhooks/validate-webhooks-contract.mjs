import crypto from "node:crypto";

import { ContractAssertionCollector } from "../runner/assertions.mjs";
import { createReport } from "../runner/contract-report.mjs";
import { Http, schema, Sloppy, TestHttp, Webhooks } from "../../../stdlib/sloppy/index.js";

const SUBSYSTEM = "webhooks";

class WebhookDb {
    constructor() {
        this.subscriptions = [];
        this.outbox = [];
        this.attempts = [];
        this.statements = [];
        this.events = [];
    }

    __debug() {
        return Object.freeze({ kind: "fake-data-provider", placeholderStyle: "question", webhooksTestProvider: true });
    }

    async exec(sql, params = []) {
        this.statements.push(sql);
        if (sql.startsWith("insert into sloppy_webhook_subscriptions")) {
            this.subscriptions.push({
                id: params[0],
                tenantId: params[1],
                eventName: params[2],
                endpointUrl: params[3],
                secretRef: params[4],
                enabled: params[5],
                headersJson: params[6],
                allowPrivateNetworks: params[7],
                createdAt: params[8],
                updatedAt: params[9],
            });
            return { affectedRows: 1 };
        }
        if (sql.startsWith("update sloppy_webhook_subscriptions set endpoint_url")) {
            const row = this.subscriptions.find((entry) => entry.id === params[5]);
            if (row === undefined) {
                return { affectedRows: 0 };
            }
            row.endpointUrl = params[0];
            row.secretRef = params[1];
            row.headersJson = params[2];
            row.allowPrivateNetworks = params[3];
            row.updatedAt = params[4];
            return { affectedRows: 1 };
        }
        if (sql.startsWith("update sloppy_webhook_subscriptions set enabled")) {
            const row = this.subscriptions.find((entry) => entry.id === params[2]);
            if (row === undefined) {
                return { affectedRows: 0 };
            }
            row.enabled = params[0];
            row.updatedAt = params[1];
            return { affectedRows: 1 };
        }
        if (sql.startsWith("insert into sloppy_webhook_outbox")) {
            this.outbox.push({
                id: params[0],
                eventName: params[1],
                eventVersion: params[2],
                payloadJson: params[3],
                payloadHash: params[4],
                occurredAt: params[5],
                availableAt: params[6],
                status: params[7],
                attemptCount: params[8],
                maxAttempts: params[9],
                nextAttemptAt: params[10],
                lockedBy: params[11],
                lockedUntil: params[12],
                idempotencyKey: params[13],
                metadataJson: params[14],
                tenantId: params[15],
                createdAt: params[16],
                updatedAt: params[17],
            });
            return { affectedRows: 1 };
        }
        if (sql.startsWith("update sloppy_webhook_outbox set status = 'delivering'")) {
            const row = this.outbox.find((entry) => entry.id === params[3]);
            if (
                row === undefined ||
                (row.status !== "pending" && row.status !== "failed") ||
                (row.lockedUntil !== null && row.lockedUntil > params[4])
            ) {
                return { affectedRows: 0 };
            }
            row.status = "delivering";
            row.lockedBy = params[0];
            row.lockedUntil = params[1];
            row.updatedAt = params[2];
            return { affectedRows: 1 };
        }
        if (sql.startsWith("insert into sloppy_webhook_delivery_attempts")) {
            this.attempts.push({
                id: params[0],
                outboxId: params[1],
                subscriptionId: params[2],
                deliveryId: params[3],
                attemptNumber: params[4],
                status: params[5],
                statusCode: params[6],
                errorCode: params[7],
                errorMessage: params[8],
                requestHeadersJson: params[9],
                responsePreview: params[10],
                durationMs: params[11],
                attemptedAt: params[12],
            });
            return { affectedRows: 1 };
        }
        if (sql.startsWith("update sloppy_webhook_outbox set status = ?")) {
            const row = this.outbox.find((entry) => entry.id === params[4]);
            if (row === undefined) {
                return { affectedRows: 0 };
            }
            row.status = params[0];
            row.attemptCount = params[1];
            row.nextAttemptAt = params[2];
            row.lockedBy = null;
            row.lockedUntil = null;
            row.updatedAt = params[3];
            return { affectedRows: 1 };
        }
        return { affectedRows: 0 };
    }

    async query(sql, params = []) {
        this.statements.push(sql);
        if (sql.startsWith("select id, tenant_id as tenantId") && sql.includes("from sloppy_webhook_subscriptions where (? is null")) {
            const event = params[0];
            return this.subscriptions.filter((entry) => event === null || entry.eventName === event);
        }
        if (sql.startsWith("select id, event_name as eventName")) {
            return this.outbox.filter((entry) => entry.status === "pending" || entry.status === "failed").slice(0, params[3]);
        }
        if (sql.includes("from sloppy_webhook_subscriptions where event_name = ?")) {
            return this.subscriptions.filter((entry) => entry.eventName === params[0] && entry.enabled === 1);
        }
        if (sql.includes("select distinct subscription_id")) {
            return [...new Set(
                this.attempts
                    .filter((entry) => entry.outboxId === params[0] && entry.status === "delivered")
                    .map((entry) => entry.subscriptionId),
            )].map((subscriptionId) => ({ subscriptionId }));
        }
        return [];
    }

    async queryOne(sql, params = []) {
        this.statements.push(sql);
        if (sql.includes("from sloppy_webhook_subscriptions where id = ?")) {
            return this.subscriptions.find((entry) => entry.id === params[0]) ?? null;
        }
        if (sql.includes("secret_ref as secretRef from sloppy_webhook_subscriptions")) {
            const row = this.subscriptions.find((entry) => entry.id === params[0]);
            return row === undefined ? null : { secretRef: row.secretRef };
        }
        if (sql.includes("from sloppy_webhook_outbox where idempotency_key = ?")) {
            return null;
        }
        if (sql.includes("from sloppy_webhook_outbox where id = ? and locked_by = ?")) {
            return this.outbox.find((entry) => entry.id === params[0] && entry.lockedBy === params[1] && entry.status === "delivering") ?? null;
        }
        return null;
    }

    async transaction(callback) {
        const snapshot = {
            subscriptions: structuredClone(this.subscriptions),
            outbox: structuredClone(this.outbox),
            attempts: structuredClone(this.attempts),
        };
        this.events.push("begin");
        try {
            const value = await callback(this);
            this.events.push("commit");
            return value;
        } catch (error) {
            this.subscriptions = snapshot.subscriptions;
            this.outbox = snapshot.outbox;
            this.attempts = snapshot.attempts;
            this.events.push("rollback");
            throw error;
        }
    }
}

const OrderCreated = Webhooks.event("order.created", {
    version: 1,
    schema: schema.object({
        orderId: schema.string(),
        total: schema.number(),
    }),
});

function installNodeCrypto() {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = {
        ...(previous ?? {}),
        crypto: {
            ...(previous?.crypto ?? {}),
            randomUuid() {
                return crypto.randomUUID();
            },
            randomBytes(length) {
                return new Uint8Array(crypto.randomBytes(length));
            },
            hash(algorithm, bytes) {
                return new Uint8Array(crypto.createHash(algorithm).update(Buffer.from(bytes)).digest());
            },
            hmac(algorithm, key, bytes) {
                return new Uint8Array(crypto.createHmac(algorithm, Buffer.from(key)).update(Buffer.from(bytes)).digest());
            },
            constantTimeEquals(left, right) {
                const a = Buffer.from(left);
                const b = Buffer.from(right);
                return a.length === b.length && crypto.timingSafeEqual(a, b);
            },
        },
    };
    return () => {
        if (previous === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previous;
        }
    };
}

function createWebhookApp(db, options = {}) {
    const builder = Sloppy.createBuilder();
    builder.services.addSingleton("data.main", () => db);
    builder.services.addHttpClient(Http.client("webhooks", { baseUrl: "https://receiver.example" }));
    builder.services.addWebhooks(Webhooks.outbox({
        provider: "main",
        providerKind: "test",
        signingSecret: options.signingSecret ?? "test-secret",
        delivery: {
            client: "webhooks",
            maxResponsePreviewBytes: options.maxResponsePreviewBytes,
            retry: options.retry ?? Webhooks.retry.exponential({
                maxAttempts: options.maxAttempts ?? 2,
                initialDelayMs: 0,
                maxDelayMs: 0,
                jitter: false,
            }),
        },
    }));
    return builder.build();
}

async function expectFailure(fn, pattern) {
    try {
        await fn();
    } catch (error) {
        if (pattern === undefined || pattern.test(String(error?.message ?? error))) {
            return;
        }
        throw new Error(`expected failure matching ${pattern}, got ${error?.message ?? error}`);
    }
    throw new Error("expected operation to fail");
}

async function check(collector, invariant, message, fn) {
    try {
        await fn();
        collector.pass(invariant, message);
    } catch (error) {
        collector.fail(invariant, `${message}: ${error?.message ?? error}`);
    }
}

function assert(condition, message) {
    if (!condition) {
        throw new Error(message);
    }
}

async function publishOrder(db, webhooks, orderId = "ord_contract") {
    return db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId, total: 42 }));
}

async function runDescriptorContract(collector) {
    await check(collector, "webhooks.event.descriptor-valid", "event descriptors are stable, versioned, schema-backed, and reject invalid shapes", async () => {
        assert(OrderCreated.name === "order.created", "event name drifted");
        assert(OrderCreated.version === 1, "event version missing");
        assert(typeof OrderCreated.schema?.validate === "function", "event schema metadata missing");
        assert(OrderCreated.validate({ orderId: "ord_descriptor", total: 1 }).orderId === "ord_descriptor", "descriptor validation failed");
        await expectFailure(() => Webhooks.event("bad", { version: 1, schema: OrderCreated.schema }), /dotted/u);
        await expectFailure(() => Webhooks.event("order.invalid", { version: 0, schema: OrderCreated.schema }), /positive/u);
        await expectFailure(() => Webhooks.event("order.invalid", { version: 1, schema: {} }), /schema/u);
        await expectFailure(() => OrderCreated.validate({ orderId: 123, total: 1 }), /SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED/u);
    });
}

async function runOutboxContracts(collector) {
    await check(collector, "webhooks.outbox.commit-publishes", "committed publishes create deterministic outbox rows matching the descriptor and payload", async () => {
        const db = new WebhookDb();
        const webhooks = createWebhookApp(db).services.get(Webhooks.token());
        const published = await publishOrder(db, webhooks, "ord_commit");
        assert(db.events.at(-1) === "commit", "transaction did not commit");
        assert(db.outbox.length === 1, "commit did not create one outbox row");
        const row = db.outbox[0];
        assert(row.id === published.id, "published metadata did not match row id");
        assert(row.eventName === "order.created", "event name not persisted");
        assert(row.eventVersion === 1, "event version not persisted");
        assert(row.payloadJson === JSON.stringify({ orderId: "ord_commit", total: 42 }), "payload JSON drifted");
        assert(typeof row.payloadHash === "string" && row.payloadHash.length === 64, "payload hash missing");
        assert(row.status === "pending", "initial status must be pending");
        assert(row.attemptCount === 0, "initial attempt count must be zero");
        assert(row.maxAttempts === 2, "max attempts did not come from retry policy");
        assert(row.lockedBy === null && row.lockedUntil === null && row.nextAttemptAt === null, "initial lock/retry fields must be null");
        assert(row.metadataJson === "{}", "metadata JSON must be deterministic by default");
    });

    await check(collector, "webhooks.outbox.rollback-suppresses", "rolled-back publishes do not leave outbox rows behind", async () => {
        const db = new WebhookDb();
        const webhooks = createWebhookApp(db).services.get(Webhooks.token());
        await expectFailure(
            () => db.transaction(async (tx) => {
                await webhooks.publish(tx, OrderCreated, { orderId: "ord_rollback", total: 42 });
                throw new Error("rollback");
            }),
            /rollback/u,
        );
        assert(db.events.at(-1) === "rollback", "transaction did not roll back");
        assert(db.outbox.length === 0, "rollback still published an outbox row");
    });
}

async function createSubscribedService(options = {}) {
    const db = new WebhookDb();
    const webhooks = createWebhookApp(db, options).services.get(Webhooks.token());
    await webhooks.subscriptions.create({
        id: options.subscriptionId ?? "sub_contract",
        event: "order.created",
        url: options.url ?? "https://receiver.example/webhooks/orders",
        secret: options.secret ?? "receiver-secret",
        headers: options.headers,
    });
    await publishOrder(db, webhooks, options.orderId ?? "ord_delivery");
    return { db, webhooks };
}

async function runDeliveryContracts(collector) {
    await check(collector, "webhooks.delivery.success", "2xx delivery records an attempt and marks the outbox delivered", async () => {
        const { db, webhooks } = await createSubscribedService({ orderId: "ord_success" });
        const mock = TestHttp.mock().post("/webhooks/orders").replyJson(200, { ok: true });
        const summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert(summary.delivered === 1, "summary did not count delivered attempt");
        assert(db.outbox[0].status === "delivered", "outbox row not marked delivered");
        assert(db.attempts.length === 1 && db.attempts[0].status === "delivered", "delivery attempt not recorded as delivered");
        mock.expectCalled("POST", "/webhooks/orders").expectNoUnexpectedCalls();
    });

    await check(collector, "webhooks.delivery.retryable", "retryable HTTP and network failures remain retryable while attempts remain", async () => {
        const retryableHttp = await createSubscribedService({ orderId: "ord_retry_http" });
        const httpMock = TestHttp.mock().post("/webhooks/orders").replyJson(500, { retry: true }, { "retry-after": "0" });
        const httpSummary = await retryableHttp.webhooks.deliverPending({ client: httpMock.createClient("webhooks") });
        assert(httpSummary.failed === 1, "retryable HTTP failure was not counted as failed");
        assert(retryableHttp.db.outbox[0].status === "failed", "retryable HTTP failure dead-lettered immediately");
        assert(retryableHttp.db.outbox[0].attemptCount === 1, "retryable HTTP attempt count wrong");

        const network = await createSubscribedService({ orderId: "ord_retry_network" });
        const networkMock = TestHttp.mock().post("/webhooks/orders").connectionError();
        const networkSummary = await network.webhooks.deliverPending({ client: networkMock.createClient("webhooks") });
        assert(networkSummary.failed === 1, "network error was not treated as retryable");
        assert(network.db.outbox[0].status === "failed", "network error dead-lettered before attempts were exhausted");
        assert(network.db.attempts[0].errorCode === "SLOPPY_E_HTTP_CONNECT_FAILED", "network attempt error code not recorded");
    });

    await check(collector, "webhooks.delivery.dead-letter", "non-retryable 4xx and exhausted retries move to dead_letter", async () => {
        const terminal = await createSubscribedService({ orderId: "ord_404" });
        const terminalMock = TestHttp.mock().post("/webhooks/orders").replyJson(404, { ok: false });
        const terminalSummary = await terminal.webhooks.deliverPending({ client: terminalMock.createClient("webhooks") });
        assert(terminalSummary.deadLetter === 1 && terminalSummary.failed === 0, "non-retryable failure retried instead of dead-lettering");
        assert(terminal.db.outbox[0].status === "dead_letter", "non-retryable failure did not dead-letter");

        const exhausted = await createSubscribedService({
            orderId: "ord_exhausted",
            retry: Webhooks.retry.fixed({ maxAttempts: 1, delayMs: 0, retryOnStatus: [500], jitter: false }),
        });
        const exhaustedMock = TestHttp.mock().post("/webhooks/orders").replyJson(500, { retry: true });
        const exhaustedSummary = await exhausted.webhooks.deliverPending({ client: exhaustedMock.createClient("webhooks") });
        assert(exhaustedSummary.deadLetter === 1, "exhausted retry did not dead-letter");
        assert(exhausted.db.outbox[0].status === "dead_letter", "exhausted row status is not dead_letter");
    });

    await check(collector, "webhooks.delivery.retry-after", "Retry-After controls the next retry time for retryable responses", async () => {
        const { db, webhooks } = await createSubscribedService({ orderId: "ord_retry_after" });
        const mock = TestHttp.mock().post("/webhooks/orders").replyJson(429, { retry: true }, { "retry-after": "3" });
        const started = Date.now();
        const summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert(summary.failed === 1, "429 was not treated as retryable");
        assert(Date.parse(db.outbox[0].nextAttemptAt) - started >= 2500, "Retry-After was not reflected in nextAttemptAt");
    });
}

async function runSigningContracts(collector) {
    await check(collector, "webhooks.signing.exact-bytes", "outbound signatures cover the exact payload bytes", async () => {
        const payload = "{\"z\":1,\"a\":\"space kept\"}";
        const signed = await Webhooks.sign(payload, {
            secret: "exact-secret",
            event: "order.created",
            id: "delivery_exact",
            timestamp: "2000",
            attempt: 1,
        });
        const expected = crypto.createHmac("sha256", "exact-secret").update("2000.").update(payload).digest("hex");
        assert(signed.signature === `v1=${expected}`, "signature does not match exact bytes");
        await Webhooks.verify({
            headers: signed.headers,
            bytes() {
                return new TextEncoder().encode(payload);
            },
        }, {
            secret: "exact-secret",
            toleranceMs: 1000,
            nowMs: 2000 * 1000,
        });
    });

    await check(collector, "webhooks.verify.bad-signature-rejected", "wrong secrets, modified bodies, missing signatures, and replay are rejected", async () => {
        const body = JSON.stringify({ orderId: "ord_verify", total: 42 });
        const signed = await Webhooks.sign(body, {
            secret: "verify-secret",
            event: "order.created",
            id: "delivery_verify",
            timestamp: "3000",
        });
        const request = (headers = signed.headers, text = body) => ({
            headers,
            bytes() {
                return new TextEncoder().encode(text);
            },
        });
        await Webhooks.verify(request(), { secret: "verify-secret", toleranceMs: 1000, nowMs: 3000 * 1000 });
        await expectFailure(
            () => Webhooks.verify(request(), { secret: "wrong-secret", toleranceMs: 1000, nowMs: 3000 * 1000 }),
            /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/u,
        );
        await expectFailure(
            () => Webhooks.verify(request(signed.headers, JSON.stringify({ orderId: "ord_modified", total: 42 })), {
                secret: "verify-secret",
                toleranceMs: 1000,
                nowMs: 3000 * 1000,
            }),
            /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/u,
        );
        const { "Sloppy-Webhook-Signature": _signature, ...missingSignatureHeaders } = signed.headers;
        await expectFailure(
            () => Webhooks.verify(request(missingSignatureHeaders), { secret: "verify-secret", toleranceMs: 1000, nowMs: 3000 * 1000 }),
            /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/u,
        );
        const seen = new Set();
        const dedupe = {
            seen(id) {
                return seen.has(id);
            },
            mark(id) {
                seen.add(id);
            },
        };
        await Webhooks.verify(request(), { secret: "verify-secret", toleranceMs: 1000, nowMs: 3000 * 1000, dedupe });
        await expectFailure(
            () => Webhooks.verify(request(), { secret: "verify-secret", toleranceMs: 1000, nowMs: 3000 * 1000, dedupe }),
            /SLOPPY_E_WEBHOOK_REPLAY_DETECTED/u,
        );
    });

    await check(collector, "webhooks.verify.timestamp-tolerance", "old timestamps are rejected according to tolerance", async () => {
        const body = JSON.stringify({ ok: true });
        const signed = await Webhooks.sign(body, {
            secret: "timestamp-secret",
            event: "order.created",
            timestamp: "4000",
        });
        const request = {
            headers: new Map(Object.entries(signed.headers).map(([name, value]) => [name.toLowerCase(), value])),
            text() {
                return body;
            },
        };
        await Webhooks.verify(request, { secret: "timestamp-secret", toleranceMs: 1000, nowMs: 4000 * 1000 });
        await expectFailure(
            () => Webhooks.verify(request, { secret: "timestamp-secret", toleranceMs: 1000, nowMs: 4002 * 1000 }),
            /SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE/u,
        );
    });
}

async function runDestinationAndSecretContracts(collector) {
    await check(collector, "webhooks.destination.safe", "subscription destinations reject unsafe URL shapes and private networks by default", async () => {
        const db = new WebhookDb();
        const webhooks = createWebhookApp(db).services.get(Webhooks.token());
        const base = { event: "order.created", secret: "receiver-secret" };
        await expectFailure(() => webhooks.subscriptions.create({ ...base, url: "ftp://receiver.example/hook" }), /http:\/\/ or https:\/\//u);
        await expectFailure(() => webhooks.subscriptions.create({ ...base, url: "https://user:pass@receiver.example/hook" }), /userinfo|fragment/u);
        await expectFailure(() => webhooks.subscriptions.create({ ...base, url: "https://receiver.example/hook#fragment" }), /userinfo|fragment/u);
        await expectFailure(() => webhooks.subscriptions.create({ ...base, url: "http://127.0.0.1/hook" }), /private or loopback/u);
        await expectFailure(() => webhooks.subscriptions.create({ ...base, url: "http://192.168.1.10/hook" }), /private or loopback/u);
        const local = await webhooks.subscriptions.create({
            ...base,
            url: "http://127.0.0.1/hook",
            allowPrivateNetworks: true,
        });
        assert(local.allowPrivateNetworks === true, "private-network override not recorded");
    });

    await check(collector, "webhooks.secret.not-returned", "subscription secrets are not returned from get or list", async () => {
        const db = new WebhookDb();
        const webhooks = createWebhookApp(db).services.get(Webhooks.token());
        const created = await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/orders",
            secret: "receiver-secret",
        });
        const fetched = await webhooks.subscriptions.get(created.id);
        const listed = await webhooks.subscriptions.list({ event: "order.created" });
        assert(!("secret" in created) && !("secret" in fetched) && !("secret" in listed[0]), "public subscription object exposed a secret field");
        assert(!JSON.stringify({ created, fetched, listed }).includes("receiver-secret"), "public subscription JSON leaked the secret value");
    });
}

async function runDiagnosticsContract(collector) {
    await check(collector, "webhooks.diagnostics.redacted", "delivery diagnostics redact secrets and bound response previews", async () => {
        const { db, webhooks } = await createSubscribedService({
            orderId: "ord_diagnostics",
            secret: "receiver-secret",
            maxResponsePreviewBytes: 8,
        });
        const mock = TestHttp.mock().post("/webhooks/orders").replyText(500, "0123456789abcdef");
        await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        const attempt = db.attempts[0];
        assert(attempt !== undefined, "delivery attempt was not recorded");
        assert(attempt.responsePreview === "01234567", "response preview was not bounded");
        assert(!attempt.requestHeadersJson.includes("test-secret"), "fallback signing secret leaked in request diagnostics");
        assert(!attempt.requestHeadersJson.includes("receiver-secret"), "subscription signing secret leaked in request diagnostics");
        assert(!attempt.requestHeadersJson.includes("v1="), "webhook signature leaked in request diagnostics");
        assert(JSON.parse(attempt.requestHeadersJson)["Sloppy-Webhook-Signature"] === "[REDACTED]", "webhook signature header was not redacted");
        await expectFailure(
            () => webhooks.subscriptions.create({
                event: "order.created",
                url: "https://receiver.example/webhooks/auth",
                secret: "receiver-secret",
                headers: {
                    Authorization: "Bearer token",
                    Cookie: "sid=secret",
                    "Set-Cookie": "sid=secret",
                },
            }),
            /sensitive delivery headers/u,
        );
    });
}

export async function runWebhooksContract({ tier }) {
    const startedAt = new Date().toISOString();
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "pr" });
    const restoreCrypto = installNodeCrypto();
    try {
        await runDescriptorContract(collector);
        await runOutboxContracts(collector);
        await runDeliveryContracts(collector);
        await runSigningContracts(collector);
        await runDestinationAndSecretContracts(collector);
        await runDiagnosticsContract(collector);
    } finally {
        restoreCrypto();
    }
    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings: collector.findings,
    });
}
