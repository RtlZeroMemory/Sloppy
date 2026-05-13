import { Config, Http, Results, Sloppy, Webhooks, schema } from "sloppy";

const OrderCreated = Webhooks.event("order.created", {
    version: 1,
    schema: schema.object({
        orderId: schema.string(),
        customerId: schema.string(),
        total: schema.number(),
    }),
});

const builder = Sloppy.createBuilder();

builder.services.addHttpClient(Http.client("webhooks", {
    baseUrl: Config.required("Webhooks:BaseUrl"),
    retry: Http.retry.none(),
}));

builder.services.addWebhooks(Webhooks.outbox({
    provider: "main",
    signingSecret: Config.requiredSecret("Webhooks:SigningSecret"),
    delivery: {
        client: "webhooks",
        retry: Webhooks.retry.exponential({
            maxAttempts: 8,
            initialDelayMs: 1000,
            maxDelayMs: 300000,
        }),
    },
}));

const app = builder.build();

app.post("/orders", async (ctx) => {
    const order = await ctx.services.get("data.main").transaction(async (tx) => {
        const created = { id: "ord_1", customerId: "cus_1", total: 42 };
        await ctx.webhooks.publish(tx, OrderCreated, {
            orderId: created.id,
            customerId: created.customerId,
            total: created.total,
        });
        return created;
    });

    return Results.created(`/orders/${order.id}`, order);
});

export default app;
