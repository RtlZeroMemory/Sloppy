import assert from "node:assert/strict";

import {
    Cache,
    Http,
    Redis,
    Results,
    Schema,
    Sloppy,
    TestHost,
    TestHttp,
} from "../../stdlib/sloppy/index.js";

{
    const builder = Sloppy.createBuilder();
    const disposalOrder = [];
    let scopedCreated = 0;
    let transientCreated = 0;
    let singletonDisposed = 0;

    builder.services.addSingleton("singleton", () => ({
        value: "root",
        dispose() {
            singletonDisposed += 1;
            disposalOrder.push("singleton");
        },
    }));
    builder.services.addScoped("scoped", () => {
        scopedCreated += 1;
        const id = scopedCreated;
        return {
            id,
            dispose() {
                disposalOrder.push(`scoped:${id}`);
            },
        };
    });
    builder.services.addTransient("transient", () => {
        transientCreated += 1;
        const id = transientCreated;
        return {
            id,
            dispose() {
                disposalOrder.push(`transient:${id}`);
            },
        };
    });

    assert.throws(() => builder.services.addSingleton("singleton", {}), /already registered/);
    assert.throws(() => builder.services.addScoped("", () => ({})), /service token/);
    const app = builder.build();
    assert.throws(() => app.services.get("scoped"), /root service resolution only supports singleton/);

    app.get("/services", (ctx) => {
        const scopedA = ctx.services.get("scoped");
        const scopedB = ctx.services.get("scoped");
        const transientA = ctx.services.get("transient");
        const transientB = ctx.services.get("transient");
        let missingMessage = "";
        try {
            ctx.services.get("missing");
        } catch (error) {
            missingMessage = error.message;
        }
        return Results.json({
            singleton: ctx.services.get("singleton").value,
            scopedStableWithinRequest: scopedA === scopedB,
            scopedId: scopedA.id,
            transientIds: [transientA.id, transientB.id],
            transientDistinct: transientA !== transientB,
            missing: ctx.services.tryGet("missing") === undefined,
            missingMessage,
        });
    });

    const host = await TestHost.create(app);
    try {
        const first = await host.get("/services");
        first.expectStatus(200);
        assert.deepEqual(first.json(), {
            singleton: "root",
            scopedStableWithinRequest: true,
            scopedId: 1,
            transientIds: [1, 2],
            transientDistinct: true,
            missing: true,
            missingMessage: "Sloppy service 'missing' is not registered.",
        });
        assert.deepEqual(disposalOrder, ["transient:2", "transient:1", "scoped:1"]);

        const second = await host.get("/services");
        second.expectStatus(200);
        assert.equal(second.json().scopedId, 2);
        assert.deepEqual(disposalOrder.slice(-3), ["transient:4", "transient:3", "scoped:2"]);
    } finally {
        await host.close();
    }

    assert.equal(singletonDisposed, 1);
    assert.throws(() => app.services.get("singleton"), /service provider is disposed/);
}

{
    const duplicate = Sloppy.createBuilder();
    duplicate.services.addCache(Cache.memory("default", { maxEntries: 4 }));
    assert.throws(() => duplicate.services.addCache(Cache.memory("default", { maxEntries: 4 })), /already registered/);
    duplicate.services.addRedis(Redis.client("main", { url: "redis://localhost:6379/0" }));
    assert.throws(() => duplicate.services.addRedis(Redis.client("main", { url: "redis://localhost:6379/0" })), /already registered/);
    await duplicate.build().services.dispose();
}

{
    const Invoice = Schema.object({
        id: Schema.string(),
        status: Schema.string(),
    });
    const Billing = Http.typedClient("billing", {
        baseUrl: "http://billing.example.test",
        endpoints: {
            getInvoice: Http.get("/invoices/{id}")
                .params(Schema.object({ id: Schema.string() }))
                .returns(200, Invoice),
        },
    });
    const app = Sloppy.create();
    app.services.addHttpClient(Http.client("billing", { baseUrl: "http://billing.example.test" }));
    app.services.addHttpClient(Billing);

    app.get("/integrations", async (ctx) => {
        const billing = ctx.services.get(Billing);
        const invoice = await billing.getInvoice({ id: "inv_1" });
        const cache = ctx.services.get(Cache.token("default"));
        await cache.set("invoice:inv_1", invoice.status);
        const redis = ctx.services.get(Redis.token("main"));
        return Results.json({
            invoice,
            cached: await cache.get("invoice:inv_1"),
            redis: await redis.ping(),
            plain: ctx.services.get("plain").value,
            freshRedisTokenStable: Redis.token("main").__sloppyRedisToken === Redis.token("main").__sloppyRedisToken,
            redisTokenObjectsAreFresh: Redis.token("main") !== Redis.token("main"),
        });
    });

    let redisClosed = 0;
    const redis = Object.freeze({
        name: "main",
        async ping() {
            return "PONG";
        },
        close() {
            redisClosed += 1;
        },
    });
    const mock = TestHttp.mock()
        .get("/invoices/inv_1")
        .replyJson(200, { id: "inv_1", status: "paid" });
    const host = await TestHost.create(app, {
        services: {
            plain: Object.freeze({ value: "override" }),
        },
        caches: {
            default: Cache.memory("override", { maxEntries: 8 }),
        },
        redis: {
            main: redis,
        },
        httpClients: {
            billing: mock,
        },
    });

    try {
        const response = await host.get("/integrations");
        response.expectStatus(200).expectJson({
            invoice: { id: "inv_1", status: "paid" },
            cached: "paid",
            redis: "PONG",
            plain: "override",
            freshRedisTokenStable: true,
            redisTokenObjectsAreFresh: true,
        });
        mock.expectCalled("GET", "/invoices/inv_1").expectNoUnexpectedCalls();
    } finally {
        await host.close();
    }
    assert.equal(redisClosed, 1);
}
