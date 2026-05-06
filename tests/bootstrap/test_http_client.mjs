import assert from "node:assert/strict";
import net from "node:net";

import { HttpClient } from "../../stdlib/sloppy/index.js";

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
    const server = net.createServer(async (socket) => {
        socket.on("error", () => {});
        try {
            const request = await readSocketRequest(socket);
            const response = await handler(request);
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

await assertRejectsMessage(
    () => HttpClient.get("https://127.0.0.1/"),
    /SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE/,
);

await assertRejectsMessage(
    () => HttpClient.request({ url: "http://127.0.0.1/", text: "a", bytes: new Uint8Array([1]) }),
    /SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY/,
);

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/\r\nx-test: injected"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_URL/,
);

await assertRejectsMessage(
    () => HttpClient.create({ baseUrl: "http://127.0.0.1" }).get("/ok\r\nx-test: injected"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_URL/,
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
