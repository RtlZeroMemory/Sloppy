# Webhooks

Sloppy ships a first-party webhook subsystem for both outbound delivery and
inbound verification. Outbound delivery uses a provider-backed **outbox**: a
publish inside your business transaction writes an outbox row, and a separate
worker signs and POSTs each enabled subscription. Delivery is **at least once**
— receivers must be idempotent.

This page is the user-facing guide. The full API surface is documented at
[Core APIs / Webhooks](../api/webhooks.md), and the reference table at
[Reference / Webhooks](../reference/webhooks.md).

## Define an event

Events are stable, versioned descriptors with a Sloppy schema. The schema is
validated before any outbox row is written.

```ts
import { Webhooks, schema } from "sloppy";

export const OrderCreated = Webhooks.event("order.created", {
    version: 1,
    schema: schema.object({
        orderId: schema.string(),
        customerId: schema.string(),
        total: schema.number(),
    }),
});
```

Names are dotted identifiers (`order.created`, `invoice.paid`). Versions are
positive integers; bump the version when the schema changes in a breaking way.

## Register the outbox

The outbox needs a data provider for storage, an HTTP client for delivery, and
a signing secret. Register it through `services.addWebhooks(...)`:

```ts
import { Config, Http, Sloppy, Webhooks } from "sloppy";

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
```

The outbox provisions four provider-backed tables:
`sloppy_webhook_subscriptions`, `sloppy_webhook_outbox`,
`sloppy_webhook_delivery_attempts`, and `sloppy_webhook_inbound_dedup`. Use
`Webhooks.sql(provider)` to read the SQL templates if you need them for
custom migrations.

Configure the delivery HTTP client with `Http.retry.none()` — retry is owned by
the webhook subsystem, not by hidden HTTP-client retry.

## Publish inside a transaction

`ctx.webhooks.publish(tx, event, payload)` writes the outbox row inside the
same database transaction as the business change, so a successful commit
guarantees the event is durable. Validation runs first; invalid payloads throw
`SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED` and never write a row.

```ts
app.post("/orders", async (ctx) => {
    const order = await ctx.services.get("data.main").transaction(async (tx) => {
        const created = await createOrder(tx, ctx.body);
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

For idempotent producers, pass `idempotencyKey`:

```ts
await ctx.webhooks.publish(tx, OrderCreated, payload, {
    idempotencyKey: `order:${order.id}:created`,
});
```

The provider schema enforces one outbox row per key — repeated publish calls
return the existing event metadata.

## Manage subscriptions

Subscriptions are managed through `ctx.webhooks.subscriptions` (or the
resolved service):

```ts
await webhooks.subscriptions.create({
    event: "order.created",
    url: "https://partner.example.com/webhooks/orders",
    secret: Config.requiredSecret("Customer:WebhookSecret"),
    headers: { "X-Customer": "acme" },
});
```

Supported operations: `create`, `get`, `list`, `update`, `enable`, `disable`,
`delete`. Subscription secrets sign deliveries for that subscription when
present; the outbox `signingSecret` is the fallback. Secrets are never
returned from `get` or `list`, and partial updates preserve the existing
secret unless `secret` is supplied again.

User-configured endpoints reject loopback, localhost, link-local, and private
network destinations by default. Set `allowPrivateNetworks: true` only for
trusted local receivers in tests or private deployments.

## Deliver pending events

Delivery runs through `webhooks.deliverPending()` or the job descriptor. Wire
it through a background service, a scheduled trigger you own, or a worker
queue depending on how you operate the app:

```ts
import { Webhooks } from "sloppy";

await Webhooks.jobs.deliverPending({ batchSize: 100 }).run({ webhooks });
```

Each call claims a batch of pending rows with a lease, sends one signed POST
per enabled subscription, records a delivery-attempt row, and updates the
outbox status. Statuses are `pending`, `delivering`, `delivered`, `failed`,
and `dead_letter`.

Retry behavior:

- HTTP `2xx` → success.
- Default retryable statuses: `408`, `425`, `429`, `500`, `502`, `503`, `504`.
  Setting `retryOnStatus` **replaces** that list, not appends.
- `400`, `401`, `403`, `404`, and other non-retryable responses move the row
  to `dead_letter` after a single attempt is recorded.
- Network errors are retryable while attempts remain.
- `Retry-After` is honored for retryable responses.

## Outbound signatures

Sloppy deliveries set the following headers:

- `Sloppy-Webhook-Id`
- `Sloppy-Webhook-Event`
- `Sloppy-Webhook-Timestamp`
- `Sloppy-Webhook-Signature`
- `Sloppy-Webhook-Attempt`

Signature format is `v1=<hex hmac sha256>` computed over `timestamp + "." + body`
using the exact JSON bytes sent to the receiver.

## Verify inbound webhooks

`Webhooks.verify(ctxOrRequest, options)` validates HMAC-SHA256, the timestamp
tolerance, the exact body bytes, and (optionally) replay deduplication. It
returns the delivery id, event name, timestamp, parsed payload, raw body, and
headers:

```ts
app.post("/integrations/sloppy", async (ctx) => {
    const event = await Webhooks.verify(ctx, {
        secret: Config.requiredSecret("Inbound:WebhookSecret"),
        toleranceMs: 300_000,
    });
    return Results.ok({ received: event.id });
});
```

Pass `secrets: [current, previous]` to support secret rotation.

## Test webhooks

Run the bootstrap webhook tests for executable coverage:

```sh
node tests/bootstrap/test_webhooks.mjs
```

For app-level integration tests, the public surface is shaped to be testable
with `TestHost` and `TestServices` — see [TestHost](../api/testhost.md) and
[TestServices](../api/testservices.md). Use `Webhooks.sign(payload, options)`
to generate signature headers when sending fixture requests through TestHost.

## Errors

Webhook failures throw `SloppyWebhookError` with stable codes:
`SLOPPY_E_WEBHOOK_INVALID_OPTIONS`,
`SLOPPY_E_WEBHOOK_EVENT_VALIDATION_FAILED`,
`SLOPPY_E_WEBHOOK_OUTBOX_UNAVAILABLE`,
`SLOPPY_E_WEBHOOK_DELIVERY_FAILED`,
`SLOPPY_E_WEBHOOK_SIGNATURE_INVALID`,
`SLOPPY_E_WEBHOOK_REPLAY_DETECTED`,
`SLOPPY_E_WEBHOOK_TIMESTAMP_OUT_OF_RANGE`,
`SLOPPY_E_WEBHOOK_SECRET_UNAVAILABLE`, and
`SLOPPY_E_WEBHOOK_CLOSED`.

## Examples

- `examples/webhooks-basic` — event descriptor, outbox registration,
  transactional publish, and signed delivery wiring

Bootstrap tests under `tests/bootstrap/test_webhooks.mjs` cover the
end-to-end shape. Current support boundaries are tracked in the
[stability matrix](../reference/stability.md).
