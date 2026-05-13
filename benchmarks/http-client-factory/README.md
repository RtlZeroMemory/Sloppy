# HTTP Client Factory Benchmark

`bench-http-client-factory.mjs` reports two lanes:

- JavaScript factory overhead against `TestHttp` mocks.
- Native HTTP/1 loopback transport through the JavaScript stdlib bridge,
  comparing one-off `HttpClient.get` calls with a named factory client using a
  one-connection pool.

The loopback lane reports server connection counts and factory pool counters so
reviewers can see whether the factory path reused transport resources. Benchmark
output is measurement data only; it is not correctness coverage.

Run:

```powershell
node benchmarks/http-client-factory/bench-http-client-factory.mjs
```

Use `SLOPPY_HTTP_FACTORY_BENCH_REQUESTS` to set mock iterations and
`SLOPPY_HTTP_FACTORY_LOOPBACK_REQUESTS` to set loopback iterations.
