import assert from "node:assert/strict";
import net from "node:net";

import { CancellationController, Deadline, HttpClient } from "../../stdlib/sloppy/index.js";

async function assertRejectsMessage(fn, expected) {
    await assert.rejects(fn, (error) => {
        assert.match(String(error.message), expected);
        return true;
    });
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

function startHttpServer(handler) {
    let nextConnectionId = 1;
    const server = net.createServer(async (socket) => {
        const connectionId = nextConnectionId;
        nextConnectionId += 1;
        socket.on("error", () => {});
        try {
            const request = await readSocketRequest(socket);
            const response = await handler(request, { connectionId });
            socket.end(response);
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
                close: () => new Promise((done) => server.close(done)),
            });
        });
    });
}

function startPersistentHttpServer(handler) {
    let nextConnectionId = 1;
    const sockets = new Set();
    const server = net.createServer(async (socket) => {
        const connectionId = nextConnectionId;
        nextConnectionId += 1;
        sockets.add(socket);
        socket.on("close", () => sockets.delete(socket));
        socket.on("error", () => {});
        try {
            while (!socket.destroyed) {
                const request = await readSocketRequest(socket);
                const response = await handler(request, { connectionId });
                socket.write(response);
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

    function resolveRead(handle) {
        if (handle.waiters.length === 0 || handle.chunks.length === 0) {
            return;
        }
        const waiter = handle.waiters.shift();
        waiter.resolve(readFromHandle(handle, waiter.maxBytes));
    }

    function readFromHandle(handle, maxBytes) {
        const first = handle.chunks[0];
        if (first.byteLength <= maxBytes) {
            return handle.chunks.shift();
        }
        handle.chunks[0] = first.subarray(maxBytes);
        return first.subarray(0, maxBytes);
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
                if (options.host === "dns.invalid") {
                    reject(new Error("SLOPPY_E_NET_DNS_FAILURE: deterministic test host"));
                    return;
                }
                const socket = net.createConnection({ host: options.host, port: options.port });
                const id = nextId;
                nextId += 1;
                const handle = {
                    id,
                    socket,
                    chunks: [],
                    waiters: [],
                    ended: false,
                };

                socket.once("connect", () => {
                    handles.set(id, handle);
                    resolve({ id });
                });
                socket.once("error", (error) => {
                    const mapped = new Error(`SLOPPY_E_NET_CONNECT_FAILED: ${error.message}`);
                    if (handles.has(handle.id)) {
                        for (const waiter of handle.waiters.splice(0)) {
                            waiter.reject(mapped);
                        }
                    }
                    reject(mapped);
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
        await fn();
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

await withNodeNetBridge(async () => {
    let observed;
    const server = await startHttpServer((request) => {
        observed = request;
        return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        const response = await HttpClient.get(`${server.url}/health?ready=1`, {
            headers: { "x-test": "1" },
        });

        assert.equal(response.status, 200);
        assert.equal(response.statusText, "OK");
        assert.equal(response.headers.get("content-type"), "text/plain");
        assert.equal(await response.text(), "ok");
        await assertRejectsMessage(() => response.bytes(), /SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED/);
        assert.equal(observed.method, "GET");
        assert.equal(observed.target, "/health?ready=1");
        assert.equal(observed.version, "HTTP/1.1");
        assert.equal(observed.headers.get("host"), server.url.slice("http://".length));
        assert.equal(observed.headers.get("x-test"), "1");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\n\r\nclose-delimited");

    try {
        const response = await HttpClient.get(`${server.url}/close-delimited`);

        assert.equal(response.status, 200);
        assert.equal(await response.text(), "close-delimited");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");

    try {
        const response = await HttpClient.get(`${server.url}/chunked`);

        assert.equal(response.status, 200);
        assert.equal(await response.text(), "hello");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startPersistentHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId, connection: request.headers.get("connection") });
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.equal(await (await client.get("/one")).text(), "ok");
        assert.equal(await (await client.get("/two")).text(), "ok");

        assert.deepEqual(observed.map((request) => request.target), ["/one", "/two"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 1);
        assert.deepEqual(observed.map((request) => request.connection), ["keep-alive", "keep-alive"]);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startPersistentHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId });
        return "HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.equal(await (await client.get("/first")).text(), "ok");
        assert.equal(await (await client.get("/second")).text(), "ok");

        assert.deepEqual(observed.map((request) => request.target), ["/first", "/second"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 2);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startPersistentHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId });
        return "HTTP/1.1 204 No Content\r\n\r\n";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.deepEqual(await (await client.get("/empty-one", { timeoutMs: 1000 })).bytes(), new Uint8Array(0));
        assert.deepEqual(await (await client.get("/empty-two", { timeoutMs: 1000 })).bytes(), new Uint8Array(0));

        assert.deepEqual(observed.map((request) => request.target), ["/empty-one", "/empty-two"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 1);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId });
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.equal(await (await client.get("/stale-one")).text(), "ok");
        assert.equal(await (await client.get("/stale-two")).text(), "ok");

        assert.deepEqual(observed.map((request) => request.target), ["/stale-one", "/stale-two"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 2);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let releaseHold;
    const holdReleased = new Promise((resolve) => {
        releaseHold = resolve;
    });
    let observedHold;
    const holdObserved = new Promise((resolve) => {
        observedHold = resolve;
    });
    const server = await startPersistentHttpServer(async (request) => {
        if (request.target === "/hold") {
            observedHold();
            await holdReleased;
        }
        return "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        const held = client.get("/hold");
        await holdObserved;
        await assertRejectsMessage(() => client.get("/second"), /SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED/);
        releaseHold();
        await held;
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let redirectedHeaders;
    const target = await startHttpServer((request) => {
        redirectedHeaders = request.headers;
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });
    const redirect = await startHttpServer(() => `HTTP/1.1 302 Found\r\nLocation: ${target.url}/final\r\nContent-Length: 0\r\n\r\n`);

    try {
        const response = await HttpClient.get(`${redirect.url}/start`, {
            headers: {
                Authorization: "Bearer SECRET",
                Cookie: "session=SECRET",
                "X-Api-Key": "SECRET",
                "X-Trace": "visible",
            },
        });

        assert.equal(await response.text(), "ok");
        assert.equal(redirectedHeaders.get("authorization"), undefined);
        assert.equal(redirectedHeaders.get("cookie"), undefined);
        assert.equal(redirectedHeaders.get("x-api-key"), undefined);
        assert.equal(redirectedHeaders.get("x-trace"), "visible");
    } finally {
        await redirect.close();
        await target.close();
    }
});

await withNodeNetBridge(async () => {
    let redirectedRequest;
    const target = await startHttpServer((request) => {
        redirectedRequest = request;
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });
    const redirect = await startHttpServer(() => `HTTP/1.1 303 See Other\r\nLocation: ${target.url}/final\r\nContent-Length: 0\r\n\r\n`);

    try {
        const response = await HttpClient.post(`${redirect.url}/start`, {
            text: "abc",
            redirects: { allowPost: true },
        });

        assert.equal(await response.text(), "ok");
        assert.equal(redirectedRequest.method, "GET");
        assert.equal(redirectedRequest.body.byteLength, 0);
        assert.equal(redirectedRequest.headers.get("content-length"), undefined);
        assert.equal(redirectedRequest.headers.get("content-type"), undefined);
    } finally {
        await redirect.close();
        await target.close();
    }
});

await withNodeNetBridge(async () => {
    const target = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    const redirect = await startHttpServer(() => `HTTP/1.1 302 Found\r\nLocation: ${target.url}/final\r\nContent-Length: 0\r\n\r\n`);

    try {
        await assertRejectsMessage(
            () =>
                HttpClient.get(`${redirect.url}/start`, {
                    headers: { Authorization: "Bearer SECRET" },
                    redirects: { crossOriginSensitiveHeaders: "deny" },
                }),
            /SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED/,
        );
    } finally {
        await redirect.close();
        await target.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 302 Found\r\nLocation: /loop\r\nContent-Length: 0\r\n\r\n");

    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/loop`),
            /SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP/,
        );
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer((request) => {
        const current = Number(request.target.slice("/r".length));
        return `HTTP/1.1 302 Found\r\nLocation: /r${current + 1}\r\nContent-Length: 0\r\n\r\n`;
    });

    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/r1`, { redirects: { max: 2 } }),
            /SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED/,
        );
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    await assertRejectsMessage(
        () => HttpClient.get("http://127.0.0.1:9/denied", { network: { strict: true, allow: [] } }),
        /SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED/,
    );
});

await withNodeNetBridge(async () => {
    await assertRejectsMessage(
        () => HttpClient.get("http://dns.invalid/"),
        /SLOPPY_E_HTTP_CLIENT_DNS_FAILED/,
    );
});

await withNodeNetBridge(async () => {
    let observed;
    const server = await startHttpServer((request) => {
        observed = request;
        return "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 18\r\n\r\n{\"id\":1,\"ok\":true}";
    });

    try {
        const client = HttpClient.create({ baseUrl: `${server.url}/v1/` });
        const value = await client.getJson("users/1?active=1");

        assert.deepEqual(value, { id: 1, ok: true });
        assert.equal(observed.method, "GET");
        assert.equal(observed.target, "/v1/users/1?active=1");
        assert.equal(observed.headers.get("accept"), "application/json");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");

    try {
        assert.equal(await HttpClient.text(`${server.url}/text`), "hello");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc");

    try {
        assert.deepEqual(Array.from(await HttpClient.bytes(`${server.url}/bytes`)), [97, 98, 99]);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let observed;
    const server = await startHttpServer((request) => {
        observed = request;
        return "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            headers: { "user-agent": "slop-test" },
        });
        const response = await client.post("/items", { text: "abc" });

        assert.equal(response.status, 201);
        assert.deepEqual(await response.bytes(), new Uint8Array(0));
        assert.equal(observed.method, "POST");
        assert.equal(observed.target, "/items");
        assert.equal(observed.headers.get("user-agent"), "slop-test");
        assert.equal(observed.headers.get("content-length"), "3");
        assert.equal(observed.body.toString("utf8"), "abc");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let observed;
    const server = await startHttpServer((request) => {
        observed = request;
        return "HTTP/1.1 201 Created\r\nContent-Type: application/json\r\nContent-Length: 8\r\n\r\n{\"id\":1}";
    });

    try {
        const response = await HttpClient.postJson(`${server.url}/items`, { name: "test" });

        assert.equal(response.status, 201);
        assert.deepEqual(await response.json(), { id: 1 });
        assert.equal(observed.method, "POST");
        assert.equal(observed.target, "/items");
        assert.equal(observed.headers.get("content-type"), "application/json");
        assert.equal(observed.headers.get("content-length"), "15");
        assert.equal(observed.body.toString("utf8"), "{\"name\":\"test\"}");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let observed;
    async function* bodyStream() {
        yield new Uint8Array([97, 98]);
        yield new Uint8Array([99]);
    }
    const server = await startHttpServer((request) => {
        observed = request;
        return "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
    });

    try {
        const response = await HttpClient.post(`${server.url}/stream`, { stream: bodyStream() });

        assert.equal(response.status, 204);
        assert.equal(observed.headers.get("content-type"), "application/octet-stream");
        assert.equal(observed.headers.get("content-length"), "3");
        assert.equal(observed.body.toString("utf8"), "abc");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello");

    try {
        const response = await HttpClient.get(`${server.url}/response-stream`);
        const chunks = [];
        for await (const chunk of response.stream({ chunkSize: 2 })) {
            chunks.push(Array.from(chunk));
        }

        assert.deepEqual(chunks, [[104, 101], [108, 108], [111]]);
        await assertRejectsMessage(() => response.text(), /SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED/);
    } finally {
        await server.close();
    }
});

await assertRejectsMessage(
    () => HttpClient.get("https://127.0.0.1/"),
    /SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE/,
);

await assertRejectsMessage(
    () => HttpClient.request({ url: "http://127.0.0.1/", text: "a", bytes: new Uint8Array([1]) }),
    /SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY/,
);

await assertRejectsMessage(
    () => HttpClient.request({ url: "http://127.0.0.1/", json: {}, text: "a" }),
    /SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY/,
);

await assertRejectsMessage(
    () => HttpClient.request({ url: "http://127.0.0.1/", stream: {} }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await assertRejectsMessage(
    () =>
        HttpClient.request(
            {
                url: "http://127.0.0.1/",
                stream: (async function* tooLargeStream() {
                    yield new Uint8Array([1, 2]);
                    yield new Uint8Array([3, 4]);
                })(),
                maxRequestBytes: 3,
            },
        ),
    /SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT/,
);

await assertRejectsMessage(
    () =>
        HttpClient.request({
            url: "http://127.0.0.1/",
            stream: (async function* stalledStream() {
                await new Promise(() => {});
            })(),
            timeoutMs: 10,
        }),
    /SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT/,
);

{
    const circular = {};
    circular.self = circular;
    await assertRejectsMessage(
        () => HttpClient.request({ url: "http://127.0.0.1/", json: circular }),
        /SLOPPY_E_HTTP_CLIENT_INVALID_JSON/,
    );
}

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/\r\nx-test: injected"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_URL/,
);

await assertRejectsMessage(
    () => HttpClient.create({ baseUrl: "http://127.0.0.1" }).get("/ok\r\nx-test: injected"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_URL/,
);

await assertRejectsMessage(
    () => HttpClient.getJson("http://127.0.0.1/", { headers: "accept: application/json" }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "wat\r\n\r\n");
    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/malformed`),
            /SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE/,
        );
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbo");
    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/truncated`),
            /SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE/,
        );
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 8\r\n\r\nnot json");
    try {
        await assertRejectsMessage(
            () => HttpClient.getJson(`${server.url}/invalid-json`),
            /SLOPPY_E_HTTP_CLIENT_INVALID_JSON/,
        );
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const server = net.createServer(async (socket) => {
        socket.on("error", () => {});
        await readSocketRequest(socket);
    });
    const baseUrl = await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            server.off("error", reject);
            const address = server.address();
            resolve(`http://127.0.0.1:${address.port}`);
        });
    });

    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${baseUrl}/timeout`, { timeoutMs: 20 }),
            /SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT/,
        );
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
});

await withNodeNetBridge(async () => {
    const server = net.createServer(async (socket) => {
        socket.on("error", () => {});
        await readSocketRequest(socket);
    });
    const baseUrl = await new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            server.off("error", reject);
            const address = server.address();
            resolve(`http://127.0.0.1:${address.port}`);
        });
    });

    try {
        const controller = new CancellationController();
        const request = HttpClient.get(`${baseUrl}/cancelled`, { signal: controller.signal });
        controller.cancel("test cancellation");
        await assertRejectsMessage(() => request, /SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED/);
    } finally {
        await new Promise((resolve) => server.close(resolve));
    }
});

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/deadline", { deadline: Deadline.after(0) }),
    /SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT/,
);

await withNodeNetBridge(async () => {
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nbody");
    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/too-large`, { maxResponseBytes: 3 }),
            /SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT/,
        );
    } finally {
        await server.close();
    }
});
