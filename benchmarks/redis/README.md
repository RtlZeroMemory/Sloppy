# Redis Benchmarks

These local benchmarks cover Sloppy's Redis JavaScript stdlib helpers. They do
not benchmark a Redis server and do not make production performance claims.

```powershell
node benchmarks/redis/bench-redis.mjs
```

The script reports:

- RESP command encoding throughput;
- RESP response parser throughput;
- cache key/tag bookkeeping overhead using a fake command sink.

Use the numbers only for local regression tracking on the same machine.
