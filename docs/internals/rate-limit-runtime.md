# Rate Limit Runtime

Rate limiting is enforced in the app-host route handler after auth middleware
has had a chance to populate `ctx.user` and before the user handler runs.
Authenticated partitions fail clearly when no authenticated user is present.

The default store is a bounded memory store registered as `default` and
`memory`. Apps can register named stores with
`app.services.addRateLimitStore(name, store)`.

Store keys combine policy name, algorithm, and a hash of the partition value.
Metrics and diagnostics receive only low-cardinality route and policy metadata
plus the partition hash. They never receive raw user IDs, IPs, API keys,
Authorization headers, Cookie values, or arbitrary header values.

WebSocket app-host handshakes pass through the same route handler, so a denied
upgrade returns a route rejection before the socket handler can call
`accept()`. Per-message WebSocket throttling is not exposed yet because the
current socket API has no route-level message policy hook.

The Redis store surface exists as a fail-closed adapter. It does not silently
fall back to memory when distributed storage is requested.
