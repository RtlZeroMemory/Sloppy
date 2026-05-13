# Webhook Runtime

Stability: Experimental (pre-alpha). The documented `publish(tx, event,
payload)` and `deliverPending()` semantics are the contract for this surface,
but the runtime remains pre-alpha.

The webhook runtime is a JavaScript stdlib layer over Sloppy services, data
providers, crypto, HttpClientFactory, and worker-style jobs.

It does not deliver from the request transaction. `publish(tx, event, payload)`
validates and inserts an outbox row through the supplied transaction. Delivery
is performed by `deliverPending()` after the transaction commits.

## Storage

The storage contract uses four tables:

- `sloppy_webhook_subscriptions`
- `sloppy_webhook_outbox`
- `sloppy_webhook_delivery_attempts`
- `sloppy_webhook_inbound_dedup`

DDL and statement templates are available through `Webhooks.sql("sqlite")`,
`Webhooks.sql("postgres")`, and `Webhooks.sql("sqlserver")`.

The SQL templates use provider-appropriate placeholder styles and DDL. SQL
Server uses bounded `nvarchar` columns for indexed fields and conditional
`sys.indexes` checks. PostgreSQL uses boolean columns for subscription flags.
Tests assert that the migration templates contain the expected tables and
indexes.

## Delivery

`deliverPending()` claims pending rows with `locked_by` and `locked_until`,
loads enabled subscriptions by event name, and records attempts per
subscription. A row is terminal when all deliveries are terminal or retry
attempts are exhausted.

Failures do not drop events. Retryable failures move the outbox row to
`failed` with `next_attempt_at`. Exhausted rows move to `dead_letter`.
Non-retryable HTTP responses move to `dead_letter` immediately. Retryable
responses honor `Retry-After`; when multiple subscriptions fail, the next
attempt uses the largest retry delay observed in the fanout.

## Signatures

Outbound signatures use HMAC-SHA256:

```text
v1 = HMAC_SHA256(secret, timestamp + "." + body)
```

Verification compares hex digests with a constant-time loop and enforces
timestamp tolerance before optional replay deduplication.
