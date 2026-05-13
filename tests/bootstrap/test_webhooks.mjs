import assert from "node:assert/strict";
import crypto from "node:crypto";

import {
    Http,
    Results,
    schema,
    Sloppy,
    SloppyWebhookError,
    TestHost,
    TestHttp,
    Webhooks,
} from "../../stdlib/sloppy/index.js";

const previousSloppy = globalThis.__sloppy;
globalThis.__sloppy = {
    ...(previousSloppy ?? {}),
    crypto: {
        ...(previousSloppy?.crypto ?? {}),
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
            if (row !== undefined) {
                row.endpointUrl = params[0];
                row.secretRef = params[1];
                row.headersJson = params[2];
                row.allowPrivateNetworks = params[3];
                row.updatedAt = params[4];
                return { affectedRows: 1 };
            }
            return { affectedRows: 0 };
        }
        if (sql.startsWith("update sloppy_webhook_subscriptions set enabled")) {
            const row = this.subscriptions.find((entry) => entry.id === params[2]);
            if (row !== undefined) {
                row.enabled = params[0];
                row.updatedAt = params[1];
                return { affectedRows: 1 };
            }
            return { affectedRows: 0 };
        }
        if (sql.startsWith("delete from sloppy_webhook_subscriptions")) {
            const before = this.subscriptions.length;
            this.subscriptions = this.subscriptions.filter((entry) => entry.id !== params[0]);
            return { affectedRows: before - this.subscriptions.length };
        }
        if (sql.startsWith("insert into sloppy_webhook_outbox")) {
            if (params[13] !== null && this.outbox.some((entry) => entry.idempotencyKey === params[13])) {
                const error = new Error("unique constraint failed: sloppy_webhook_outbox.idempotency_key");
                error.code = "SQLITE_CONSTRAINT_UNIQUE";
                throw error;
            }
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
            if (this.blockClaims === true) {
                return { affectedRows: 0 };
            }
            const row = this.outbox.find((entry) => entry.id === params[3]);
            if (row !== undefined &&
                (row.status === "pending" || row.status === "failed") &&
                (row.lockedUntil === null || row.lockedUntil <= params[4]))
            {
                row.status = "delivering";
                row.lockedBy = params[0];
                row.lockedUntil = params[1];
                row.updatedAt = params[2];
                return { affectedRows: 1 };
            }
            return { affectedRows: 0 };
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
            if (row !== undefined) {
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
            const row = this.outbox.find((entry) => entry.idempotencyKey === params[0]);
            return row === undefined
                ? null
                : {
                    id: row.id,
                    eventName: row.eventName,
                    eventVersion: row.eventVersion,
                    payloadHash: row.payloadHash,
                };
        }
        if (sql.includes("from sloppy_webhook_outbox where id = ? and locked_by = ?")) {
            const row = this.outbox.find((entry) => entry.id === params[0] && entry.lockedBy === params[1] && entry.status === "delivering");
            return row ?? null;
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

function createWebhookApp(db) {
    const builder = Sloppy.createBuilder();
    builder.services.addSingleton("data.main", () => db);
    builder.services.addHttpClient(Http.client("webhooks", { baseUrl: "https://receiver.example" }));
    builder.services.addWebhooks(Webhooks.outbox({
        provider: "main",
        providerKind: "test",
        signingSecret: "test-secret",
        delivery: {
            client: "webhooks",
            retry: Webhooks.retry.exponential({
                maxAttempts: 2,
                initialDelayMs: 0,
                maxDelayMs: 0,
                jitter: false,
            }),
        },
    }));
    return builder.build();
}

const OrderCreated = Webhooks.event("order.created", {
    version: 1,
    schema: schema.object({
        orderId: schema.string(),
        total: schema.number(),
    }),
});

try {
    {
        assert.equal(typeof Webhooks.event, "function");
        assert.equal(typeof Webhooks.outbox, "function");
        assert.equal(typeof Webhooks.retry.fixed, "function");
        assert.equal(SloppyWebhookError.name, "SloppyWebhookError");
        assert.throws(() => Webhooks.event("bad", { version: 1, schema: OrderCreated.schema }), /dotted/);
        assert.throws(() => Webhooks.retry.exponential({ maxAttempts: 0 }), /positive/);
        assert.throws(() => Webhooks.outbox({ provider: "main", signingSecret: "" }), /signingSecret/);
        assert.match(Webhooks.sql("sqlite").createOutbox, /sloppy_webhook_outbox/);
        assert.match(Webhooks.sql("postgres").insertOutbox, /\$1/);
        assert.match(Webhooks.sql("sqlserver").createAttempts, /dbo\.sloppy_webhook_delivery_attempts/);
        assert.doesNotMatch(Webhooks.sql("sqlserver").createSubscriptions, /nvarchar\(max\).*event_name/u);
        assert.match(Webhooks.sql("sqlserver").indexes.join("\n"), /if not exists .*create index/u);
        assert.match(Webhooks.sql("sqlite").indexes.join("\n"), /idempotency_key\) where idempotency_key is not null/u);
        assert.match(Webhooks.sql("postgres").createSubscriptions, /enabled boolean not null/u);
        assert.match(Webhooks.sql("postgres").subscriptionsForEvent, /enabled = true/u);
        const builder = Sloppy.createBuilder();
        const unapprovedDb = new WebhookDb();
        unapprovedDb.__debug = () => Object.freeze({ kind: "fake-data-provider", placeholderStyle: "question" });
        builder.services.addSingleton("data.main", () => unapprovedDb);
        builder.services.addWebhooks(Webhooks.outbox({ provider: "main", signingSecret: "secret" }));
        await assert.rejects(
            () => builder.build().services.get(Webhooks.token()).init(),
            /requires a real data provider|fake providers/u,
        );
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        const subscription = await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/orders",
            secret: "receiver-secret",
            headers: { "X-Customer": "acme" },
        });
        assert.equal(subscription.event, "order.created");
        assert.deepEqual(subscription.headers, { "X-Customer": "acme" });
        assert.equal("secret" in subscription, false);
        assert.equal(await webhooks.subscriptions.get(subscription.id).then((row) => row.id), subscription.id);
        assert.equal((await webhooks.subscriptions.list({ event: "order.created" })).length, 1);
        await assert.rejects(
            () => webhooks.subscriptions.create({
                event: "order.created",
                url: "http://127.0.0.1/hook",
                secret: "secret",
            }),
            /private or loopback/,
        );
        await assert.rejects(
            () => webhooks.subscriptions.create({
                event: "order.created",
                url: "http://127.0.0.2/hook",
                secret: "secret",
            }),
            /private or loopback/,
        );
        await assert.rejects(
            () => webhooks.subscriptions.create({
                event: "order.created",
                url: "http://[fe80::1]/hook",
                secret: "secret",
            }),
            /private or loopback/,
        );
        await webhooks.subscriptions.disable(subscription.id);
        assert.equal((await webhooks.subscriptions.get(subscription.id)).enabled, false);
        await webhooks.subscriptions.enable(subscription.id);
        assert.equal((await webhooks.subscriptions.get(subscription.id)).enabled, true);
        const privateSubscription = await webhooks.subscriptions.create({
            event: "order.created",
            url: "http://127.0.0.1/hook",
            secret: "secret",
            allowPrivateNetworks: true,
        });
        await webhooks.subscriptions.update(privateSubscription.id, { headers: { "X-Only": "headers" } });
        assert.equal((await webhooks.subscriptions.get(privateSubscription.id)).allowPrivateNetworks, true);
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.init();
        await assert.rejects(
            () => db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: 123, total: 42 })),
            /SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED/,
        );
        assert.equal(db.outbox.length, 0);
        await assert.rejects(
            () => db.transaction(async (tx) => {
                await webhooks.publish(tx, OrderCreated, { orderId: "ord_1", total: 42 });
                throw new Error("rollback");
            }),
            /rollback/,
        );
        assert.equal(db.outbox.length, 0);
        const published = await db.transaction((tx) =>
            webhooks.publish(tx, OrderCreated, { orderId: "ord_2", total: 43 }));
        assert.match(published.id, /^whevt_/);
        assert.equal(db.outbox.length, 1);
        assert.equal(db.outbox[0].eventName, "order.created");
        assert.equal(db.events.at(-1), "commit");
        const first = await db.transaction((tx) =>
            webhooks.publish(tx, OrderCreated, { orderId: "ord_2", total: 43 }, { idempotencyKey: "order:ord_2" }));
        const second = await db.transaction((tx) =>
            webhooks.publish(tx, OrderCreated, { orderId: "ord_2", total: 43 }, { idempotencyKey: "order:ord_2" }));
        assert.equal(first.id, second.id);
        assert.equal(second.idempotent, true);
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/orders",
            secret: "receiver-secret",
            headers: { "X-Customer": "acme" },
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_3", total: 44 }));
        const mock = TestHttp.mock()
            .post("/webhooks/orders")
            .replyJson(500, { retry: true }, { "retry-after": "0" })
            .post("/webhooks/orders")
            .replyJson(200, { ok: true });
        let summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.failed, 1);
        assert.equal(db.outbox[0].status, "failed");
        assert.equal(db.attempts.length, 1);
        assert.doesNotMatch(db.attempts[0].requestHeadersJson, /test-secret|receiver-secret/);
        summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.delivered, 1);
        assert.equal(db.outbox[0].status, "delivered");
        mock.expectCalled("POST", "/webhooks/orders").expectNoUnexpectedCalls();
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            id: "sub_delivered",
            event: "order.created",
            url: "https://receiver.example/webhooks/delivered",
            secret: "receiver-secret",
        });
        await webhooks.subscriptions.create({
            id: "sub_retry",
            event: "order.created",
            url: "https://receiver.example/webhooks/retry",
            secret: "receiver-secret",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_5", total: 46 }));
        const mock = TestHttp.mock()
            .post("/webhooks/delivered")
            .replyJson(200, { ok: true })
            .post("/webhooks/retry")
            .replyJson(500, { retry: true }, { "retry-after": "0" })
            .post("/webhooks/retry")
            .replyJson(200, { ok: true });
        let summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.delivered, 1);
        assert.equal(summary.failed, 1);
        assert.equal(db.outbox[0].status, "failed");
        summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.delivered, 1);
        assert.equal(summary.skipped, 1);
        assert.equal(db.attempts.filter((attempt) => attempt.subscriptionId === "sub_delivered").length, 1);
        mock.expectCalled("POST", "/webhooks/delivered")
            .expectCalled("POST", "/webhooks/retry")
            .expectNoUnexpectedCalls();
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/not-found",
            secret: "receiver-secret",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_404", total: 45 }));
        const mock = TestHttp.mock()
            .post("/webhooks/not-found")
            .replyJson(404, { ok: false });
        const summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.deadLetter, 1);
        assert.equal(summary.failed, 0);
        assert.equal(db.outbox[0].status, "dead_letter");
    }

    {
        const db = new WebhookDb();
        const builder = Sloppy.createBuilder();
        builder.services.addSingleton("data.main", () => db);
        builder.services.addHttpClient(Http.client("webhooks", { baseUrl: "https://receiver.example" }));
        builder.services.addWebhooks(Webhooks.outbox({
            provider: "main",
            providerKind: "test",
            signingSecret: "test-secret",
            delivery: {
                client: "webhooks",
                retry: Webhooks.retry.fixed({
                    maxAttempts: 2,
                    delayMs: 0,
                    retryOnStatus: [418],
                    jitter: false,
                }),
            },
        }));
        const webhooks = builder.build().services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/custom-retry",
            secret: "receiver-secret",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_custom", total: 45 }));
        let mock = TestHttp.mock()
            .post("/webhooks/custom-retry")
            .replyJson(500, { retry: true });
        let summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.deadLetter, 1);
        assert.equal(db.outbox[0].status, "dead_letter");

        db.outbox = [];
        db.attempts = [];
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_teapot", total: 45 }));
        mock = TestHttp.mock()
            .post("/webhooks/custom-retry")
            .replyJson(418, { retry: true });
        summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.failed, 1);
        assert.equal(db.outbox[0].status, "failed");
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/rate-limit",
            secret: "receiver-secret",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_429", total: 45 }));
        const mock = TestHttp.mock()
            .post("/webhooks/rate-limit")
            .replyJson(429, { retry: true }, { "retry-after": "3" });
        const summary = await webhooks.deliverPending({ client: mock.createClient("webhooks") });
        assert.equal(summary.failed, 1);
        assert.equal(db.outbox[0].status, "failed");
        assert.ok(Date.parse(db.outbox[0].nextAttemptAt) - Date.now() >= 2500);
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            event: "order.created",
            url: "https://receiver.example/webhooks/race",
            secret: "receiver-secret",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_race", total: 45 }));
        db.blockClaims = true;
        const summary = await webhooks.deliverPending({ client: TestHttp.mock().createClient("webhooks") });
        assert.equal(summary.claimed, 0);
        assert.equal(summary.skipped, 1);
        assert.equal(db.attempts.length, 0);
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        const webhooks = app.services.get(Webhooks.token());
        await webhooks.subscriptions.create({
            id: "sub_a",
            event: "order.created",
            url: "https://a.receiver.example/webhooks/orders?tenant=a",
            secret: "secret-a",
        });
        await webhooks.subscriptions.create({
            id: "sub_b",
            event: "order.created",
            url: "https://b.receiver.example/webhooks/orders?tenant=b",
            secret: "secret-b",
        });
        await webhooks.subscriptions.create({
            id: "sub_c",
            event: "order.created",
            url: "https://c.receiver.example/webhooks/orders?tenant=c",
        });
        await db.transaction((tx) => webhooks.publish(tx, OrderCreated, { orderId: "ord_hosts", total: 47 }));
        const mock = TestHttp.mock()
            .post("/webhooks/orders")
            .replyJson(200, { ok: true })
            .post("/webhooks/orders")
            .replyJson(200, { ok: true })
            .post("/webhooks/orders")
            .replyJson(200, { ok: true });
        const summary = await webhooks.deliverPending({
            client: mock.createClient("webhooks"),
            signingSecret: "override-fallback",
        });
        assert.equal(summary.delivered, 3);
        assert.equal(mock._calls[0].url, "https://a.receiver.example/webhooks/orders?tenant=a");
        assert.equal(mock._calls[1].url, "https://b.receiver.example/webhooks/orders?tenant=b");
        assert.equal(mock._calls[2].url, "https://c.receiver.example/webhooks/orders?tenant=c");
        await assert.doesNotReject(() => Webhooks.verify({
            headers: mock._calls[0].headers,
            async text() {
                return mock._calls[0].text;
            },
        }, {
            secret: "secret-a",
            toleranceMs: 60_000,
            nowMs: Number(mock._calls[0].headers["Sloppy-Webhook-Timestamp"]) * 1000,
        }));
        await assert.rejects(() => Webhooks.verify({
            headers: mock._calls[0].headers,
            async text() {
                return mock._calls[0].text;
            },
        }, {
            secret: "secret-b",
            toleranceMs: 60_000,
            nowMs: Number(mock._calls[0].headers["Sloppy-Webhook-Timestamp"]) * 1000,
        }), /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/);
        await assert.doesNotReject(() => Webhooks.verify({
            headers: mock._calls[1].headers,
            async text() {
                return mock._calls[1].text;
            },
        }, {
            secret: "secret-b",
            toleranceMs: 60_000,
            nowMs: Number(mock._calls[1].headers["Sloppy-Webhook-Timestamp"]) * 1000,
        }));
        await assert.doesNotReject(() => Webhooks.verify({
            headers: mock._calls[2].headers,
            async text() {
                return mock._calls[2].text;
            },
        }, {
            secret: "override-fallback",
            toleranceMs: 60_000,
            nowMs: Number(mock._calls[2].headers["Sloppy-Webhook-Timestamp"]) * 1000,
        }));
    }

    {
        const body = JSON.stringify({ ok: true });
        const signed = await Webhooks.sign(body, {
            secret: "inbound-secret",
            event: "order.created",
            id: "delivery_1",
            timestamp: "2000",
            attempt: 1,
        });
        const request = {
            headers: new Map(Object.entries(signed.headers).map(([name, value]) => [name.toLowerCase(), value])),
            bytes() {
                return new TextEncoder().encode(body);
            },
        };
        const seen = new Set();
        const dedupe = {
            seen(id) {
                return seen.has(id);
            },
            mark(id) {
                seen.add(id);
            },
        };
        const verified = await Webhooks.verify(request, {
            secret: "inbound-secret",
            toleranceMs: 1000,
            nowMs: 2000 * 1000,
            dedupe,
        });
        assert.deepEqual(verified.payload, { ok: true });
        await assert.rejects(
            () => Webhooks.verify(request, {
                secret: "inbound-secret",
                toleranceMs: 1000,
                nowMs: 2000 * 1000,
                dedupe,
            }),
            /SLOPPY_E_WEBHOOK_REPLAY_DETECTED/,
        );
        await assert.rejects(
            () => Webhooks.verify(request, {
                secret: "wrong",
                toleranceMs: 1000,
                nowMs: 2000 * 1000,
            }),
            /SLOPPY_E_WEBHOOK_SIGNATURE_INVALID/,
        );
        await assert.rejects(
            () => Webhooks.verify(request, {
                secret: "inbound-secret",
                toleranceMs: 1000,
                nowMs: 3000 * 1000,
            }),
            /SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE/,
        );

        const rawBody = new Uint8Array([0xff, 0x00, 0x61]);
        const rawTimestamp = "2001";
        const rawSignatureInput = Buffer.concat([Buffer.from(`${rawTimestamp}.`, "utf8"), Buffer.from(rawBody)]);
        const rawSignature = crypto.createHmac("sha256", "byte-secret").update(rawSignatureInput).digest("hex");
        await assert.doesNotReject(() => Webhooks.verify({
            headers: new Map([
                ["sloppy-webhook-timestamp", rawTimestamp],
                ["sloppy-webhook-signature", `v1=${rawSignature}`],
            ]),
            bytes() {
                return rawBody;
            },
        }, {
            secret: "byte-secret",
            toleranceMs: 1000,
            nowMs: Number(rawTimestamp) * 1000,
        }));

        const invalidBody = JSON.stringify({ orderId: "missing-total" });
        const invalidSigned = await Webhooks.sign(invalidBody, {
            secret: "inbound-secret",
            event: "order.created",
            id: "delivery_invalid",
            timestamp: "2002",
        });
        let invalidMarked = 0;
        await assert.rejects(
            () => Webhooks.verify({
                headers: new Map(Object.entries(invalidSigned.headers).map(([name, value]) => [name.toLowerCase(), value])),
                bytes() {
                    return new TextEncoder().encode(invalidBody);
                },
            }, {
                secret: "inbound-secret",
                toleranceMs: 1000,
                nowMs: 2002 * 1000,
                event: OrderCreated,
                dedupe: {
                    seen() {
                        return false;
                    },
                    mark() {
                        invalidMarked += 1;
                    },
                },
            }),
        );
        assert.equal(invalidMarked, 0);
    }

    {
        const db = new WebhookDb();
        const app = createWebhookApp(db);
        app.post("/orders", async (ctx) => {
            const result = await db.transaction(async (tx) => {
                await ctx.webhooks.publish(tx, OrderCreated, { orderId: "ord_4", total: 45 });
                return { id: "ord_4" };
            });
            return Results.created(`/orders/${result.id}`, result);
        });
        const host = await TestHost.create(app);
        try {
            await (await host.post("/orders").json({}).send()).expectStatus(201);
            assert.equal(db.outbox.length, 1);
        } finally {
            await host.close();
        }
    }

    {
        const app = Sloppy.create();
        app.get("/no-webhooks", (ctx) => Results.json({ available: ctx.webhooks !== undefined }));
        const host = await TestHost.create(app);
        try {
            const response = await (await host.get("/no-webhooks").send()).expectStatus(200);
            assert.deepEqual(await response.json(), { available: false });
        } finally {
            await host.close();
        }
    }
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
