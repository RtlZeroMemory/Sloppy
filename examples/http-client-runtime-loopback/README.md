# Runtime Loopback HTTP Client

This example documents the native-backed outbound loopback path used by the
low-level HTTP client tests.

```js
import { HttpClient } from "sloppy/net";

const client = HttpClient.create({
    baseUrl: "http://127.0.0.1:8080",
    pool: {
        maxConnectionsPerOrigin: 4,
        pendingQueueTimeoutMs: 1000,
    },
});

const response = await client.get("/health");
console.log(await response.text());
await client.close();
```

`tests/bootstrap/test_http_client.mjs` covers local loopback GET requests,
pool reuse, queue timeout/cancellation, client close behavior, and response body
limits using the runtime TCP bridge exposed to the bootstrap test harness.
