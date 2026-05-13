# Webhooks

`Webhooks` is the first-party API for durable outbound webhooks and inbound
signature verification.

Outbound webhooks use a provider-backed outbox. Publishing inserts an event row
inside the caller's database transaction, and delivery happens later through the
webhook worker. Sloppy provides at-least-once delivery. Receivers must be
idempotent.

```js
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
```

## Events

`Webhooks.event(name, options)` creates a descriptor. Names are stable dotted
identifiers such as `order.created`. Versions are positive integers. The schema
must be a Sloppy schema.

Publishing validates the payload before any outbox insert. Invalid payloads
throw `SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED` and do not write an outbox row.

## Outbox

Register webhooks through `builder.services.addWebhooks(Webhooks.outbox(...))`
or `app.services.addWebhooks(...)`.

The outbox creates these provider-backed tables:

- `sloppy_webhook_subscriptions`
- `sloppy_webhook_outbox`
- `sloppy_webhook_delivery_attempts`
- `sloppy_webhook_inbound_dedup`

SQLite, PostgreSQL, and SQL Server SQL templates are exposed through
`Webhooks.sql(provider)` for tests and tooling.

The outbox requires a real data provider in application code. Bootstrap tests
may opt into an explicit test provider kind, but the fake provider is not a
supported production backend.

## Subscriptions

Use `ctx.webhooks.subscriptions` or the resolved service:

```js
await webhooks.subscriptions.create({
    event: "order.created",
    url: "https://example.com/webhooks/orders",
    secret: Config.requiredSecret("Customer:WebhookSecret"),
    headers: { "X-Customer": "acme" },
});
```

Subscriptions support `create`, `get`, `list`, `update`, `enable`, `disable`,
and `delete`. A subscription secret is used for outbound signing when present;
the outbox `signingSecret` is the fallback. Secrets are not returned from `get`
or `list`, and partial updates preserve the existing secret unless `secret` is
provided again.

User-configured endpoints reject loopback, localhost, link-local, and private
network hosts by default. Set `allowPrivateNetworks: true` only for trusted
local receivers in tests or private deployments.

## Delivery

`webhooks.deliverPending()` claims pending rows with a lease, sends one signed
POST per enabled matching subscription, records a delivery attempt, and updates
the outbox status.

```js
await Webhooks.jobs.deliverPending({ batchSize: 100 }).run({ webhooks });
```

Status values are `pending`, `delivering`, `delivered`, `failed`, and
`dead_letter`. Retry behavior is owned by Webhooks, not hidden HTTP client
retry. Configure the delivery HTTP client with `Http.retry.none()`.

`retryOnStatus` is exact. The default retryable statuses are `408`, `425`,
`429`, `500`, `502`, `503`, and `504`; a custom list replaces that default.
Non-retryable HTTP responses such as `400`, `401`, `403`, and `404` are
terminal and move to `dead_letter` immediately. Network errors are retryable
while attempts remain. `Retry-After` controls the next attempt time for
retryable responses.

When `publish(..., { idempotencyKey })` is supplied, the provider schema
enforces one outbox row for that key and repeated publish calls return the
existing event metadata.

One event can have multiple subscriptions. Delivery attempts are tracked per
subscription. The event is delivered only after required subscription deliveries
are terminal.

## Signatures

Sloppy outbound deliveries include:

- `Sloppy-Webhook-Id`
- `Sloppy-Webhook-Event`
- `Sloppy-Webhook-Timestamp`
- `Sloppy-Webhook-Signature`
- `Sloppy-Webhook-Attempt`

The signature format is `v1=<hex hmac sha256>` over:

```text
timestamp + "." + body
```

The payload bytes are the exact JSON string sent to the receiver.

## Inbound Verification

```js
app.post("/integrations/sloppy", async (ctx) => {
    const event = await Webhooks.verify(ctx, {
        secret: Config.requiredSecret("Inbound:WebhookSecret"),
        toleranceMs: 300000,
    });

    return Results.ok({ received: event.id });
});
```

Verification checks HMAC-SHA256, timestamp tolerance, exact body bytes, and
optional replay deduplication. It returns the delivery id, event name,
timestamp, parsed payload, raw body text, and headers.

`secrets: [current, previous]` supports secret rotation.

## Errors

Webhook failures throw `SloppyWebhookError` with stable codes:

- `SLOPPY_E_WEBHOOK_INVALID_OPTIONS`
- `SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED`
- `SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE`
- `SLOPPY_E_WEBHOOK_DELIVERY_FAILED`
- `SLOPPY_E_WEBHOOK_SIGNATURE_INVALID`
- `SLOPPY_E_WEBHOOK_REPLAY_DETECTED`
- `SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE`
- `SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE`
- `SLOPPY_E_WEBHOOK_CLOSED`
