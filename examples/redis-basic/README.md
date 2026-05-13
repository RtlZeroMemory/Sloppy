# Redis Basic

This is an API-shape example for the first-party Redis client.

It shows Redis client creation, JSON/text/bytes values, scripts, diagnostics,
health, and bounded pooling. It does not contact Redis unless the exported
functions are called in a runtime with the Sloppy network bridge and a Redis
server.

Current limitations:

- requires a Redis server such as `TestServices.redis()` or a configured
  `REDIS_URL`;
- requires the Sloppy outbound network bridge;
- `rediss://` also requires the outbound TLS bridge;
- no npm Redis client;
- no cluster, sentinel, pub/sub, streams, or modules contract.
