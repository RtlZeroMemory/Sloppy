# Webhooks Reference

Stability: Experimental (alpha).

`Webhooks` is available from `sloppy` and `sloppy/webhooks`.

## API

| API | Purpose |
| --- | --- |
| `Webhooks.event(name, options)` | Defines an event descriptor with `version` and `schema`. |
| `Webhooks.outbox(options)` | Creates a DI registration descriptor for provider-backed webhook storage and delivery. |
| `Webhooks.retry.fixed(options)` | Fixed-delay retry policy. |
| `Webhooks.retry.exponential(options)` | Exponential retry policy with bounded delay. |
| `Webhooks.sign(payload, options)` | Produces Sloppy webhook signature headers for exact payload bytes. |
| `Webhooks.verify(ctxOrRequest, options)` | Verifies inbound Sloppy signature headers and timestamp tolerance. |
| `Webhooks.jobs.deliverPending(options)` | Creates a job-compatible pending-delivery handler. |
| `Webhooks.token(name?)` | Returns the DI token. The default token is `webhooks`. |
| `Webhooks.sql(provider)` | Returns SQLite, PostgreSQL, or SQL Server SQL templates for migrations/tests. |

## Outbox Options

```js
Webhooks.outbox({
    provider: "main",
    signingSecret: Config.requiredSecret("Webhooks:SigningSecret"),
    delivery: {
        client: "webhooks",
        batchSize: 100,
        leaseMs: 30000,
        retry: Webhooks.retry.exponential({
            maxAttempts: 8,
            initialDelayMs: 1000,
            maxDelayMs: 300000,
        }),
    },
});
```

`provider` resolves to `data.<name>` unless the token already contains a dot.
`delivery.client` resolves to `http.<name>` unless the token already contains a
dot.

`signingSecret` is required. It may be a string, `Secret`, or
`Config.required(...)`/`Config.requiredSecret(...)` reference.

## Delivery Semantics

Delivery is at least once. Sloppy does not claim exactly-once delivery.

The delivery worker:

1. Selects pending or retryable outbox rows.
2. Claims rows with a lease.
3. Loads enabled subscriptions for the event name.
4. Sends signed JSON POST requests through the named HTTP client.
5. Records one delivery-attempt row per subscription.
6. Marks rows `delivered`, `failed`, or `dead_letter`.

HTTP `2xx` is success. Retryable status codes are exactly the configured
`retryOnStatus` list. The default list is `408`, `425`, `429`, `500`, `502`,
`503`, and `504`. Custom `retryOnStatus` replaces that default list rather than
adding to it.

Non-retryable HTTP failures such as `400`, `401`, `403`, and `404` move the row
to `dead_letter` immediately after the delivery attempt is recorded. Network
errors are retryable while attempts remain. `Retry-After` is respected when a
retryable response includes it.

## Security Defaults

Subscription endpoints must be `http://` or `https://`, must not contain
userinfo or fragments, and reject private network destinations by default.

Delivery diagnostics redact sensitive request headers and never include signing
secrets. Response body previews are bounded.

Subscription secrets are used for outbound signing when configured on a
subscription. The outbox-level `signingSecret` is the fallback. Subscription
secrets are not returned from `get` or `list`.
