import net from "node:net";
import { performance } from "node:perf_hooks";

import { Http, HttpClient, schema, TestHttp } from "../../stdlib/sloppy/index.js";

const mockIterations = Number.parseInt(process.env.SLOPPY_HTTP_FACTORY_BENCH_REQUESTS ?? "1000", 10);
const loopbackIterations = Number.parseInt(process.env.SLOPPY_HTTP_FACTORY_LOOPBACK_REQUESTS ?? "100", 10);

function elapsedMs(start) {
    return performance.now() - start;
}

async function measure(name, iterations, fn) {
    const start = performance.now();
    const details = await fn();
    const ms = elapsedMs(start);
    return {
        name,
        iterations,
        totalMs: Number(ms.toFixed(3)),
        meanUs: Number(((ms * 1000) / iterations).toFixed(3)),
        ...(details ?? {}),
    };
}

function readSocketRequest(socket) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        let totalLength = 0;

        function parseIfReady() {
            const received = Buffer.concat(chunks, totalLength);
            const headerEnd = received.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            const head = received.subarray(0, headerEnd).toString("latin1");
            const lines = head.split("\r\n");
            const [method, target, version] = lines.shift().split(" ");
            const headers = new Map();
            for (const line of lines) {
                const colon = line.indexOf(":");
                if (colon > 0) {
                    headers.set(line.slice(0, colon).toLowerCase(), line.slice(colon + 1).trimStart());
                }
            }
            const contentLength = Number(headers.get("content-length") ?? "0");
            const bodyStart = headerEnd + 4;
            if (received.byteLength - bodyStart < contentLength) {
                return;
            }
            socket.off("data", onData);
            socket.off("error", reject);
            resolve({
                method,
                target,
                version,
                headers,
                body: received.subarray(bodyStart, bodyStart + contentLength),
            });
        }

        function onData(chunk) {
            chunks.push(chunk);
            totalLength += chunk.byteLength;
            parseIfReady();
        }

        socket.on("data", onData);
        socket.on("error", reject);
    });
}

function startPersistentHttpServer() {
    let nextConnectionId = 1;
    let requestCount = 0;
    const sockets = new Set();
    const server = net.createServer(async (socket) => {
        nextConnectionId += 1;
        sockets.add(socket);
        socket.on("close", () => sockets.delete(socket));
        socket.on("error", () => {});
        try {
            while (!socket.destroyed) {
                const request = await readSocketRequest(socket);
                requestCount += 1;
                const payload = JSON.stringify({ ok: true, path: request.target });
                socket.write([
                    "HTTP/1.1 200 OK",
                    "Content-Type: application/json",
                    `Content-Length: ${Buffer.byteLength(payload)}`,
                    request.headers.get("connection") === "close" ? "Connection: close" : "Connection: keep-alive",
                    "",
                    payload,
                ].join("\r\n"));
                if (request.headers.get("connection") === "close") {
                    socket.end();
                    break;
                }
            }
        } catch {
            socket.destroy();
        }
    });

    return new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            server.off("error", reject);
            const address = server.address();
            resolve({
                url: `http://127.0.0.1:${address.port}`,
                get connectionCount() {
                    return nextConnectionId - 1;
                },
                get requestCount() {
                    return requestCount;
                },
                close: () =>
                    new Promise((done) => {
                        for (const socket of sockets) {
                            socket.destroy();
                        }
                        server.close(done);
                    }),
            });
        });
    });
}

function createNodeNetBridge() {
    let nextId = 1;
    const handles = new Map();

    function readFromHandle(handle, maxBytes) {
        const first = handle.chunks[0];
        if (first.byteLength <= maxBytes) {
            return handle.chunks.shift();
        }
        handle.chunks[0] = first.subarray(maxBytes);
        return first.subarray(0, maxBytes);
    }

    function resolveRead(handle) {
        if (handle.waiters.length === 0 || handle.chunks.length === 0) {
            return;
        }
        const waiter = handle.waiters.shift();
        waiter.resolve(readFromHandle(handle, waiter.maxBytes));
    }

    function requireHandle(handleRef) {
        const handle = handles.get(handleRef.id);
        if (handle === undefined) {
            throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED");
        }
        return handle;
    }

    return {
        connect(options) {
            return new Promise((resolve, reject) => {
                const id = nextId;
                nextId += 1;
                const socket = net.createConnection({ host: options.host, port: options.port });
                const handle = { id, socket, chunks: [], waiters: [], ended: false };
                socket.once("error", (error) => {
                    if (handles.has(id)) {
                        for (const waiter of handle.waiters.splice(0)) {
                            waiter.reject(error);
                        }
                    }
                    reject(new Error(`SLOPPY_E_NET_CONNECT_FAILED: ${error.message}`));
                });
                socket.on("data", (chunk) => {
                    handle.chunks.push(new Uint8Array(chunk));
                    resolveRead(handle);
                });
                socket.on("end", () => {
                    handle.ended = true;
                    for (const waiter of handle.waiters.splice(0)) {
                        waiter.reject(new Error("SLOPPY_E_NET_CONNECTION_CLOSED"));
                    }
                });
                socket.once("connect", () => {
                    handles.set(id, handle);
                    resolve({ id });
                });
            });
        },
        write(handleRef, bytes) {
            let handle;
            try {
                handle = requireHandle(handleRef);
            } catch (error) {
                return Promise.reject(error);
            }
            return new Promise((resolve, reject) => {
                handle.socket.write(Buffer.from(bytes), (error) => {
                    if (error != null) {
                        reject(error);
                        return;
                    }
                    resolve();
                });
            });
        },
        read(handleRef, maxBytes) {
            let handle;
            try {
                handle = requireHandle(handleRef);
            } catch (error) {
                return Promise.reject(error);
            }
            if (handle.chunks.length > 0) {
                return Promise.resolve(readFromHandle(handle, maxBytes));
            }
            if (handle.ended) {
                return Promise.reject(new Error("SLOPPY_E_NET_CONNECTION_CLOSED"));
            }
            return new Promise((resolve, reject) => {
                handle.waiters.push({ maxBytes, resolve, reject });
            });
        },
        close(handleRef) {
            let handle;
            try {
                handle = requireHandle(handleRef);
            } catch (error) {
                return Promise.reject(error);
            }
            handles.delete(handleRef.id);
            handle.socket.destroy();
            return Promise.resolve();
        },
        abort(handleRef) {
            return this.close(handleRef);
        },
    };
}

async function withNodeNetBridge(fn) {
    const previousSloppy = globalThis.__sloppy;
    try {
        globalThis.__sloppy = { net: createNodeNetBridge() };
        return await fn();
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

const Payload = schema.object({
    id: schema.string(),
    ok: schema.boolean(),
});

const namedMock = TestHttp.mock()
    .get("/items/one")
    .replyJson(200, { id: "one", ok: true });
const namedClient = namedMock.createClient("bench");

const Typed = Http.typedClient("bench", {
    baseUrl: "http://bench.test",
    endpoints: {
        getItem: Http.get("/items/{id}")
            .params(schema.object({ id: schema.string() }))
            .returns(200, Payload),
    },
});
const typedClient = Typed.__sloppyHttpClientRegistration.createTyped(namedMock.createClient("bench"));

const rows = [];

rows.push(await measure("named-client-mock-json", mockIterations, async () => {
    for (let index = 0; index < mockIterations; index += 1) {
        await namedClient.get("/items/{id}", { params: { id: "one" } }).json(Payload);
    }
}));

rows.push(await measure("typed-client-mock-json", mockIterations, async () => {
    for (let index = 0; index < mockIterations; index += 1) {
        await typedClient.getItem({ id: "one" });
    }
}));

await withNodeNetBridge(async () => {
    const directServer = await startPersistentHttpServer();
    try {
        rows.push(await measure("direct-httpclient-sequential-loopback-json", loopbackIterations, async () => {
            for (let index = 0; index < loopbackIterations; index += 1) {
                await (await HttpClient.get(`${directServer.url}/items/${index}`)).json();
            }
            return {
                serverConnections: directServer.connectionCount,
                serverRequests: directServer.requestCount,
            };
        }));
    } finally {
        await directServer.close();
    }

    const pooledServer = await startPersistentHttpServer();
    const pooledClient = Http.client("bench-loopback", {
        baseUrl: pooledServer.url,
        pool: {
            maxConnectionsPerOrigin: 1,
            pendingQueueLimit: loopbackIterations,
        },
    });
    try {
        rows.push(await measure("factory-pooled-sequential-loopback-json", loopbackIterations, async () => {
            for (let index = 0; index < loopbackIterations; index += 1) {
                await pooledClient.get("/items/{id}", { params: { id: String(index) } }).json();
            }
            const metrics = pooledClient.metrics();
            return {
                serverConnections: pooledServer.connectionCount,
                serverRequests: pooledServer.requestCount,
                pool: metrics.pool,
            };
        }));
    } finally {
        await pooledClient.close();
        await pooledServer.close();
    }

    const directConcurrentServer = await startPersistentHttpServer();
    try {
        rows.push(await measure("direct-httpclient-concurrent-loopback-json", loopbackIterations, async () => {
            await Promise.all(Array.from({ length: loopbackIterations }, async (_, index) => {
                await (await HttpClient.get(`${directConcurrentServer.url}/items/${index}`)).json();
            }));
            return {
                serverConnections: directConcurrentServer.connectionCount,
                serverRequests: directConcurrentServer.requestCount,
            };
        }));
    } finally {
        await directConcurrentServer.close();
    }

    const pooledConcurrentServer = await startPersistentHttpServer();
    const pooledConcurrentClient = Http.client("bench-loopback-concurrent", {
        baseUrl: pooledConcurrentServer.url,
        pool: {
            maxConnectionsPerOrigin: 8,
            pendingQueueLimit: loopbackIterations,
        },
    });
    try {
        rows.push(await measure("factory-pooled-concurrent-loopback-json", loopbackIterations, async () => {
            await Promise.all(Array.from({ length: loopbackIterations }, async (_, index) => {
                await pooledConcurrentClient.get("/items/{id}", { params: { id: String(index) } }).json();
            }));
            const metrics = pooledConcurrentClient.metrics();
            return {
                serverConnections: pooledConcurrentServer.connectionCount,
                serverRequests: pooledConcurrentServer.requestCount,
                pool: metrics.pool,
            };
        }));
    } finally {
        await pooledConcurrentClient.close();
        await pooledConcurrentServer.close();
    }
});

console.log(JSON.stringify({
    kind: "http-client-factory-benchmark",
    note: "Loopback rows use the JavaScript stdlib HTTP/1 transport bridge and report connection reuse; benchmark output is measurement data only.",
    mockIterations,
    loopbackIterations,
    rows,
}, null, 2));
