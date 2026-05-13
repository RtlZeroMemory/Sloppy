import { Http } from "sloppy/http";

const orders = Http.client("orders", {
    baseUrl: "https://orders.internal",
    timeoutMs: 2000,
    retry: Http.retry.exponential({
        maxAttempts: 3,
        initialDelayMs: 100,
        maxDelayMs: 2000,
        jitter: true,
        retryOnStatus: [408, 429, 500, 502, 503, 504],
        retryOnMethods: ["GET", "HEAD", "PUT", "DELETE"],
    }),
    circuitBreaker: Http.circuitBreaker({
        failureRatio: 0.5,
        minimumThroughput: 20,
        samplingWindowMs: 30000,
        breakDurationMs: 30000,
    }),
    bulkhead: Http.bulkhead({
        maxConcurrent: 32,
        maxQueue: 128,
        queueTimeoutMs: 1000,
    }),
    pool: {
        maxConnectionsPerOrigin: 32,
        idleTimeoutMs: 60000,
        connectionLifetimeMs: 300000,
    },
});

async function loadOrder(id, signal) {
    return await orders
        .get("/orders/{id}", { params: { id }, signal })
        .json();
}

export { loadOrder, orders };
