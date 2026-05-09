import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import tls from "node:tls";

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

function derLength(length) {
    if (length < 128) {
        return Buffer.from([length]);
    }
    const bytes = [];
    let remaining = length;
    while (remaining > 0) {
        bytes.unshift(remaining & 0xff);
        remaining >>= 8;
    }
    return Buffer.from([0x80 | bytes.length, ...bytes]);
}

function der(tag, ...parts) {
    const content = Buffer.concat(parts);
    return Buffer.concat([Buffer.from([tag]), derLength(content.length), content]);
}

function derSequence(...parts) {
    return der(0x30, ...parts);
}

function derSet(...parts) {
    return der(0x31, ...parts);
}

function derExplicit(index, ...parts) {
    return der(0xa0 + index, ...parts);
}

function derContextPrimitive(index, bytes) {
    return der(0x80 | index, Buffer.from(bytes));
}

function derInteger(value) {
    let bytes;
    if (Buffer.isBuffer(value)) {
        bytes = Buffer.from(value);
    } else {
        bytes = [];
        let remaining = value;
        do {
            bytes.unshift(remaining & 0xff);
            remaining >>= 8;
        } while (remaining > 0);
        bytes = Buffer.from(bytes);
    }
    while (bytes.length > 1 && bytes[0] === 0 && (bytes[1] & 0x80) === 0) {
        bytes = bytes.subarray(1);
    }
    if ((bytes[0] & 0x80) !== 0) {
        bytes = Buffer.concat([Buffer.from([0]), bytes]);
    }
    return der(0x02, bytes);
}

function derBoolean(value) {
    return der(0x01, Buffer.from([value ? 0xff : 0]));
}

function derNull() {
    return der(0x05);
}

function derBitString(bytes, unusedBits = 0) {
    return der(0x03, Buffer.from([unusedBits]), Buffer.from(bytes));
}

function derOctetString(bytes) {
    return der(0x04, Buffer.from(bytes));
}

function derUtf8String(value) {
    return der(0x0c, Buffer.from(value, "utf8"));
}

function derGeneralizedTime(date) {
    const stamp = date.toISOString().replace(/[-:]|\.\d{3}|T/g, "");
    return der(0x18, Buffer.from(stamp, "ascii"));
}

function base128(value) {
    const bytes = [value & 0x7f];
    let remaining = value >> 7;
    while (remaining > 0) {
        bytes.unshift(0x80 | (remaining & 0x7f));
        remaining >>= 7;
    }
    return bytes;
}

function derOid(value) {
    const parts = value.split(".").map((part) => Number.parseInt(part, 10));
    const bytes = [parts[0] * 40 + parts[1]];
    for (const part of parts.slice(2)) {
        bytes.push(...base128(part));
    }
    return der(0x06, Buffer.from(bytes));
}

function pem(label, bytes) {
    const base64 = Buffer.from(bytes).toString("base64");
    const lines = base64.match(/.{1,64}/g) ?? [];
    return `-----BEGIN ${label}-----\n${lines.join("\n")}\n-----END ${label}-----\n`;
}

function createTestTlsMaterial() {
    const { privateKey, publicKey } = crypto.generateKeyPairSync("rsa", {
        modulusLength: 2048,
        publicExponent: 0x10001,
    });
    const now = Date.now();
    const algorithm = derSequence(derOid("1.2.840.113549.1.1.11"), derNull());
    const name = derSequence(
        derSet(derSequence(derOid("2.5.4.3"), derUtf8String("localhost"))),
    );
    const validity = derSequence(
        derGeneralizedTime(new Date(now - 24 * 60 * 60 * 1000)),
        derGeneralizedTime(new Date(now + 10 * 365 * 24 * 60 * 60 * 1000)),
    );
    const subjectAltName = derSequence(
        derContextPrimitive(2, Buffer.from("localhost", "ascii")),
        derContextPrimitive(7, Buffer.from([127, 0, 0, 1])),
    );
    const extensions = derExplicit(
        3,
        derSequence(
            derSequence(
                derOid("2.5.29.19"),
                derBoolean(true),
                derOctetString(derSequence(derBoolean(true))),
            ),
            derSequence(derOid("2.5.29.17"), derOctetString(subjectAltName)),
        ),
    );
    const serial = crypto.randomBytes(16);
    serial[0] &= 0x7f;
    const tbsCertificate = derSequence(
        derExplicit(0, derInteger(2)),
        derInteger(serial),
        algorithm,
        name,
        validity,
        name,
        publicKey.export({ type: "spki", format: "der" }),
        extensions,
    );
    const signature = crypto.sign("sha256", tbsCertificate, privateKey);
    const certificate = derSequence(tbsCertificate, algorithm, derBitString(signature));

    return {
        cert: pem("CERTIFICATE", certificate),
        key: privateKey.export({ type: "pkcs8", format: "pem" }),
    };
}

function startHttpsServer(handler, tlsMaterial) {
    let nextConnectionId = 1;
    const server = tls.createServer(
        { key: tlsMaterial.key, cert: tlsMaterial.cert },
        async (socket) => {
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
        },
    );

    return new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            server.off("error", reject);
            const address = server.address();
            resolve({
                url: `https://127.0.0.1:${address.port}`,
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

const HTTP2_PREFACE = Buffer.from("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", "ascii");
const HTTP2_FRAME_DATA = 0x0;
const HTTP2_FRAME_HEADERS = 0x1;
const HTTP2_FRAME_SETTINGS = 0x4;
const HTTP2_FLAG_END_STREAM = 0x1;
const HTTP2_FLAG_ACK = 0x1;
const HTTP2_FLAG_END_HEADERS = 0x4;

const HTTP2_HPACK_STATIC = [
    undefined,
    [":authority", ""],
    [":method", "GET"],
    [":method", "POST"],
    [":path", "/"],
    [":path", "/index.html"],
    [":scheme", "http"],
    [":scheme", "https"],
    [":status", "200"],
    [":status", "204"],
    [":status", "206"],
    [":status", "304"],
    [":status", "400"],
    [":status", "404"],
    [":status", "500"],
    ["accept-charset", ""],
    ["accept-encoding", "gzip, deflate"],
    ["accept-language", ""],
    ["accept-ranges", ""],
    ["accept", ""],
    ["access-control-allow-origin", ""],
    ["age", ""],
    ["allow", ""],
    ["authorization", ""],
    ["cache-control", ""],
    ["content-disposition", ""],
    ["content-encoding", ""],
    ["content-language", ""],
    ["content-length", ""],
    ["content-location", ""],
    ["content-range", ""],
    ["content-type", ""],
    ["cookie", ""],
    ["date", ""],
    ["etag", ""],
    ["expect", ""],
    ["expires", ""],
    ["from", ""],
    ["host", ""],
    ["if-match", ""],
    ["if-modified-since", ""],
    ["if-none-match", ""],
    ["if-range", ""],
    ["if-unmodified-since", ""],
    ["last-modified", ""],
    ["link", ""],
    ["location", ""],
    ["max-forwards", ""],
    ["proxy-authenticate", ""],
    ["proxy-authorization", ""],
    ["range", ""],
    ["referer", ""],
    ["refresh", ""],
    ["retry-after", ""],
    ["server", ""],
    ["set-cookie", ""],
    ["strict-transport-security", ""],
    ["transfer-encoding", ""],
    ["user-agent", ""],
    ["vary", ""],
    ["via", ""],
    ["www-authenticate", ""],
];

function h2Frame(type, flags, streamId, payload = Buffer.alloc(0)) {
    const frame = Buffer.alloc(9 + payload.length);
    frame[0] = (payload.length >>> 16) & 0xff;
    frame[1] = (payload.length >>> 8) & 0xff;
    frame[2] = payload.length & 0xff;
    frame[3] = type;
    frame[4] = flags;
    frame[5] = (streamId >>> 24) & 0x7f;
    frame[6] = (streamId >>> 16) & 0xff;
    frame[7] = (streamId >>> 8) & 0xff;
    frame[8] = streamId & 0xff;
    payload.copy(frame, 9);
    return frame;
}

function h2ReadFrame(buffer) {
    const length = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
    return {
        length,
        type: buffer[3],
        flags: buffer[4],
        streamId: ((buffer[5] & 0x7f) << 24) | (buffer[6] << 16) | (buffer[7] << 8) | buffer[8],
        payload: buffer.subarray(9, 9 + length),
    };
}

function hpackInteger(value, prefixBits, prefixMask) {
    const maxPrefix = (1 << prefixBits) - 1;
    const bytes = [];
    if (value < maxPrefix) {
        bytes.push(prefixMask | value);
    } else {
        bytes.push(prefixMask | maxPrefix);
        value -= maxPrefix;
        while (value >= 128) {
            bytes.push((value % 128) + 128);
            value = Math.floor(value / 128);
        }
        bytes.push(value);
    }
    return Buffer.from(bytes);
}

function hpackReadInteger(bytes, offset, prefixBits) {
    const maxPrefix = (1 << prefixBits) - 1;
    let value = bytes[offset] & maxPrefix;
    offset += 1;
    if (value !== maxPrefix) {
        return { value, offset };
    }
    let shift = 0;
    while (offset < bytes.length) {
        const byte = bytes[offset];
        offset += 1;
        value += (byte & 0x7f) * (2 ** shift);
        if ((byte & 0x80) === 0) {
            return { value, offset };
        }
        shift += 7;
    }
    throw new Error("incomplete HPACK integer");
}

function hpackString(value) {
    const bytes = Buffer.from(value, "utf8");
    return Buffer.concat([hpackInteger(bytes.length, 7, 0), bytes]);
}

function hpackReadString(bytes, offset) {
    assert.equal((bytes[offset] & 0x80), 0);
    const length = hpackReadInteger(bytes, offset, 7);
    return {
        value: bytes.subarray(length.offset, length.offset + length.value).toString("utf8"),
        offset: length.offset + length.value,
    };
}

function hpackHeader(index, value) {
    return Buffer.concat([hpackInteger(index, 4, 0), hpackString(value)]);
}

function hpackResponseHeaders(status, contentType, contentLength) {
    const statusBlock = status === 200 ? Buffer.from([0x88]) : hpackHeader(8, String(status));
    return Buffer.concat([
        statusBlock,
        hpackHeader(31, contentType),
        hpackHeader(28, String(contentLength)),
    ]);
}

function hpackDecodeRequestHeaders(block) {
    const headers = [];
    let offset = 0;
    while (offset < block.length) {
        const byte = block[offset];
        if ((byte & 0x80) !== 0) {
            const indexed = hpackReadInteger(block, offset, 7);
            offset = indexed.offset;
            headers.push(HTTP2_HPACK_STATIC[indexed.value]);
            continue;
        }
        const prefixBits = (byte & 0x40) !== 0 ? 6 : 4;
        const nameRef = hpackReadInteger(block, offset, prefixBits);
        offset = nameRef.offset;
        let name;
        if (nameRef.value === 0) {
            const decodedName = hpackReadString(block, offset);
            name = decodedName.value;
            offset = decodedName.offset;
        } else {
            name = HTTP2_HPACK_STATIC[nameRef.value][0];
        }
        const decodedValue = hpackReadString(block, offset);
        offset = decodedValue.offset;
        headers.push([name, decodedValue.value]);
    }
    return headers;
}

function startHttp2Server(handler, options = {}) {
    const sockets = new Set();
    const onSocket = (socket) => {
        let buffer = Buffer.alloc(0);
        let sawPreface = false;
        let responded = false;
        let requestHeaders = [];
        let requestBody = Buffer.alloc(0);
        sockets.add(socket);
        socket.on("close", () => sockets.delete(socket));
        socket.on("error", () => {});

        const respond = async (streamId) => {
            if (responded) {
                return;
            }
            responded = true;
            const headerMap = new Map(requestHeaders);
            const response = await handler({
                headers: headerMap,
                body: requestBody,
            });
            const status = response?.status ?? 200;
            const contentType = response?.contentType ?? "text/plain";
            const body = Buffer.from(response?.body ?? "h2 ok", "utf8");
            socket.write(
                Buffer.concat([
                    h2Frame(
                        HTTP2_FRAME_HEADERS,
                        HTTP2_FLAG_END_HEADERS,
                        streamId,
                        hpackResponseHeaders(status, contentType, body.length),
                    ),
                    h2Frame(HTTP2_FRAME_DATA, HTTP2_FLAG_END_STREAM, streamId, body),
                ]),
            );
        };

        const parse = () => {
            if (!sawPreface) {
                if (buffer.length < HTTP2_PREFACE.length) {
                    return;
                }
                assert.deepEqual(buffer.subarray(0, HTTP2_PREFACE.length), HTTP2_PREFACE);
                buffer = buffer.subarray(HTTP2_PREFACE.length);
                sawPreface = true;
                socket.write(h2Frame(HTTP2_FRAME_SETTINGS, 0, 0));
            }
            while (buffer.length >= 9) {
                const frame = h2ReadFrame(buffer);
                if (buffer.length < 9 + frame.length) {
                    return;
                }
                buffer = buffer.subarray(9 + frame.length);
                if (frame.type === HTTP2_FRAME_SETTINGS && (frame.flags & HTTP2_FLAG_ACK) === 0) {
                    socket.write(h2Frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK, 0));
                    continue;
                }
                if (frame.type === HTTP2_FRAME_HEADERS) {
                    requestHeaders = hpackDecodeRequestHeaders(frame.payload);
                    if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
                        void respond(frame.streamId).catch(() => socket.destroy());
                    }
                    continue;
                }
                if (frame.type === HTTP2_FRAME_DATA) {
                    requestBody = Buffer.concat([requestBody, frame.payload]);
                    if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
                        void respond(frame.streamId).catch(() => socket.destroy());
                    }
                }
            }
        };

        socket.on("data", (chunk) => {
            buffer = Buffer.concat([buffer, chunk]);
            parse();
        });
    };

    const tlsMaterial = options.tlsMaterial;
    const server = tlsMaterial === undefined
        ? net.createServer(onSocket)
        : tls.createServer(
            {
                key: tlsMaterial.key,
                cert: tlsMaterial.cert,
                ALPNProtocols: options.alpnProtocols ?? ["h2"],
            },
            onSocket,
        );
    const scheme = tlsMaterial === undefined ? "http" : "https";

    return new Promise((resolve, reject) => {
        server.once("error", reject);
        server.listen(0, "127.0.0.1", () => {
            server.off("error", reject);
            const address = server.address();
            resolve({
                url: `${scheme}://127.0.0.1:${address.port}`,
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

    function loadTlsMaterial(tlsOptions) {
        const ca = [];
        for (const key of ["caPath", "caBundlePath", "trustStorePath"]) {
            if (tlsOptions?.[key] !== undefined) {
                ca.push(fs.readFileSync(tlsOptions[key], "utf8"));
            }
        }
        return ca.length === 0 ? undefined : ca;
    }

    function tlsServerName(value) {
        return net.isIP(value) === 0 ? value : undefined;
    }

    function mapSocketError(error) {
        if (error?.code === "ERR_TLS_CERT_ALTNAME_INVALID") {
            return new Error(`SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH: ${error.message}`);
        }
        if (
            error?.code === "DEPTH_ZERO_SELF_SIGNED_CERT" ||
            error?.code === "UNABLE_TO_VERIFY_LEAF_SIGNATURE" ||
            error?.code === "SELF_SIGNED_CERT_IN_CHAIN"
        ) {
            return new Error(
                `SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED: ${error.message}`,
            );
        }
        return new Error(`SLOPPY_E_NET_CONNECT_FAILED: ${error.message}`);
    }

    function attachSocketHandlers(socket, handle, reject) {
        socket.once("error", (error) => {
            const mapped = mapSocketError(error);
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
    }

    function connectSocket(options, socketFactory, readyEvent) {
        return new Promise((resolve, reject) => {
            if (options.host === "dns.invalid") {
                reject(new Error("SLOPPY_E_NET_DNS_FAILURE: deterministic test host"));
                return;
            }
            const id = nextId;
            nextId += 1;
            const socket = socketFactory();
            const handle = {
                id,
                socket,
                chunks: [],
                waiters: [],
                ended: false,
            };
            attachSocketHandlers(socket, handle, reject);
            socket.once(readyEvent, () => {
                handles.set(id, handle);
                const handleRef = { id };
                if (typeof socket.alpnProtocol === "string" && socket.alpnProtocol.length > 0) {
                    handleRef.selectedProtocol = socket.alpnProtocol;
                }
                resolve(handleRef);
            });
        });
    }

    return {
        tlsCaPath: true,
        tlsCaBundlePath: true,
        tlsTrustStorePath: true,
        tlsClientCertificate: true,
        tlsInsecureSkipVerify: true,
        tlsAlpn: true,

        connect(options) {
            return connectSocket(
                options,
                () => net.createConnection({ host: options.host, port: options.port }),
                "connect",
            );
        },

        connectTls(options) {
            return connectSocket(
                options,
                () =>
                    tls.connect({
                        host: options.host,
                        port: options.port,
                        servername: tlsServerName(options.serverName),
                        ca: loadTlsMaterial(options.tls),
                        rejectUnauthorized: options.tls?.insecureSkipVerify !== true,
                        ALPNProtocols: options.alpnProtocols,
                    }),
                "secureConnect",
            );
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

async function withNetBridge(bridge, fn) {
    const previousSloppy = globalThis.__sloppy;
    try {
        globalThis.__sloppy = { net: bridge };
        await fn();
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
}

async function withNodeNetBridge(fn) {
    await withNetBridge(createNodeNetBridge(), fn);
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
    let observed;
    const server = await startHttp2Server((request) => {
        observed = request;
        return { body: "h2c ok" };
    });

    try {
        const response = await HttpClient.get(`${server.url}/h2c?ready=1`, {
            protocol: "h2c",
            headers: { "x-test": "h2c" },
        });

        assert.equal(response.status, 200);
        assert.equal(response.statusText, "");
        assert.equal(response.headers.get("content-type"), "text/plain");
        assert.equal(await response.text(), "h2c ok");
        assert.equal(observed.headers.get(":method"), "GET");
        assert.equal(observed.headers.get(":scheme"), "http");
        assert.equal(observed.headers.get(":path"), "/h2c?ready=1");
        assert.equal(observed.headers.get("x-test"), "h2c");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    let observed;
    const server = await startHttp2Server((request) => {
        observed = request;
        return { body: `posted ${request.body.toString("utf8")}` };
    });

    try {
        const response = await HttpClient.post(`${server.url}/h2c-post`, {
            protocol: "h2c",
            text: "body",
        });

        assert.equal(response.status, 200);
        assert.equal(await response.text(), "posted body");
        assert.equal(observed.headers.get(":method"), "POST");
        assert.equal(observed.headers.get("content-type"), "text/plain; charset=utf-8");
        assert.equal(observed.headers.get("content-length"), "4");
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "sloppy-http-client-h2-"));
    const trustStorePath = path.join(tempDir, "server.crt");
    const tlsMaterial = createTestTlsMaterial();
    fs.writeFileSync(trustStorePath, tlsMaterial.cert);
    let observed;
    const server = await startHttp2Server((request) => {
        observed = request;
        return { body: "h2 tls ok" };
    }, { tlsMaterial });

    try {
        const response = await HttpClient.get(`${server.url}/secure`, {
            protocol: "h2",
            tls: { trustStorePath },
        });

        assert.equal(response.status, 200);
        assert.equal(await response.text(), "h2 tls ok");
        assert.equal(observed.headers.get(":method"), "GET");
        assert.equal(observed.headers.get(":scheme"), "https");
        assert.equal(observed.headers.get(":path"), "/secure");
    } finally {
        await server.close();
        fs.rmSync(tempDir, { recursive: true, force: true });
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
    const server = await startHttpServer(() => "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n");

    try {
        const response = await HttpClient.request({ url: `${server.url}/head`, method: "HEAD" });

        assert.equal(response.status, 200);
        assert.deepEqual(await response.bytes(), new Uint8Array(0));
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startPersistentHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId });
        return "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.deepEqual(await (await client.request({ url: "/head-one", method: "HEAD" })).bytes(), new Uint8Array(0));
        assert.deepEqual(await (await client.request({ url: "/head-two", method: "HEAD" })).bytes(), new Uint8Array(0));

        assert.deepEqual(observed.map((request) => request.target), ["/head-one", "/head-two"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 1);
    } finally {
        await server.close();
    }
});

await withNodeNetBridge(async () => {
    const observed = [];
    const server = await startPersistentHttpServer((request) => {
        observed.push({
            method: request.method,
            target: request.target,
            body: request.body.toString("utf8"),
        });
        if (request.method === "HEAD") {
            return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n";
        }
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        assert.equal(await (await HttpClient.put(`${server.url}/static-put`, { text: "one" })).text(), "ok");
        assert.equal(await (await HttpClient.patch(`${server.url}/static-patch`, { text: "two" })).text(), "ok");
        assert.equal(await (await HttpClient.delete(`${server.url}/static-delete`)).text(), "ok");
        assert.deepEqual(await (await HttpClient.head(`${server.url}/static-head`)).bytes(), new Uint8Array(0));

        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.equal(await (await client.put("/client-put", { text: "three" })).text(), "ok");
        assert.equal(await (await client.patch("/client-patch", { text: "four" })).text(), "ok");
        assert.equal(await (await client.delete("/client-delete")).text(), "ok");
        assert.deepEqual(await (await client.head("/client-head")).bytes(), new Uint8Array(0));

        assert.deepEqual(observed, [
            { method: "PUT", target: "/static-put", body: "one" },
            { method: "PATCH", target: "/static-patch", body: "two" },
            { method: "DELETE", target: "/static-delete", body: "" },
            { method: "HEAD", target: "/static-head", body: "" },
            { method: "PUT", target: "/client-put", body: "three" },
            { method: "PATCH", target: "/client-patch", body: "four" },
            { method: "DELETE", target: "/client-delete", body: "" },
            { method: "HEAD", target: "/client-head", body: "" },
        ]);
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
    const server = await startPersistentHttpServer((request, context) => {
        observed.push({ target: request.target, connectionId: context.connectionId });
        if (request.target === "/dirty-empty") {
            return "HTTP/1.1 204 No Content\r\nContent-Length: 4\r\n\r\njunk";
        }
        return "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
    });

    try {
        const client = HttpClient.create({
            baseUrl: server.url,
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        assert.deepEqual(await (await client.get("/dirty-empty", { timeoutMs: 1000 })).bytes(), new Uint8Array(0));
        assert.equal(await (await client.get("/after-dirty", { timeoutMs: 1000 })).text(), "ok");

        assert.deepEqual(observed.map((request) => request.target), ["/dirty-empty", "/after-dirty"]);
        assert.equal(new Set(observed.map((request) => request.connectionId)).size, 2);
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
        assert.equal(await (await client.get("/warm")).text(), "ok");
        await assertRejectsMessage(
            () => client.post("/unsafe", { text: "abc", timeoutMs: 1000 }),
            /SLOPPY_E_HTTP_CLIENT_/,
        );

        assert.deepEqual(observed.map((request) => request.target), ["/warm"]);
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

await withNodeNetBridge(async () => {
    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "sloppy-http-client-tls-"));
    const trustStorePath = path.join(tempDir, "localhost-cert.pem");
    const tlsMaterial = createTestTlsMaterial();
    fs.writeFileSync(trustStorePath, tlsMaterial.cert);
    let observed;
    const server = await startHttpsServer((request) => {
        observed = request;
        return "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 8\r\n\r\nsecureok";
    }, tlsMaterial);

    try {
        const verified = await HttpClient.get(`${server.url}/verified`, {
            tls: { trustStorePath },
        });
        assert.equal(await verified.text(), "secureok");
        assert.equal(observed.method, "GET");
        assert.equal(observed.target, "/verified");

        const mutableTls = { trustStorePath };
        const stableClient = HttpClient.create({ baseUrl: server.url, tls: mutableTls });
        const stableDescriptor = stableClient.__sloppyHttpClientOptions;
        assert.equal(Object.isFrozen(stableDescriptor), true);
        assert.equal(Object.isFrozen(stableDescriptor.tls), true);
        assert.equal(stableDescriptor.tls.enabled, true);
        assert.equal(stableDescriptor.tls.hasTrustStorePath, true);
        assert.equal(stableDescriptor.tls.trustStorePath, undefined);
        mutableTls.trustStorePath = path.join(tempDir, "missing-cert.pem");
        const snapshot = await stableClient.get("/snapshot");
        assert.equal(await snapshot.text(), "secureok");

        const secretClient = HttpClient.create({
            baseUrl: server.url,
            tls: {
                trustStorePath,
                clientCertificatePath: "C:\\secret\\client.crt",
                clientPrivateKeyPath: "C:\\secret\\client.key",
                clientPrivateKeyPassphrase: "do-not-retain",
            },
        });
        const secretDescriptor = secretClient.__sloppyHttpClientOptions.tls;
        assert.equal(secretDescriptor.hasClientCertificate, true);
        assert.equal(secretDescriptor.hasClientPrivateKeyPassphrase, true);
        assert.equal(secretDescriptor.clientCertificatePath, undefined);
        assert.equal(secretDescriptor.clientPrivateKeyPath, undefined);
        assert.equal(secretDescriptor.clientPrivateKeyPassphrase, undefined);

        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/untrusted`),
            /SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED/,
        );

        const client = HttpClient.create({
            baseUrl: server.url,
            tls: { insecureSkipVerify: true },
            pool: { maxConnectionsPerOrigin: 1, idleTimeoutMs: 1000 },
        });
        const insecure = await client.head("/metadata");
        assert.equal(insecure.status, 200);
        assert.deepEqual(await insecure.bytes(), new Uint8Array(0));
    } finally {
        await server.close();
        fs.rmSync(tempDir, { recursive: true, force: true });
    }
});

await assertRejectsMessage(
    () => HttpClient.get("https://127.0.0.1/"),
    /SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE/,
);

await withNetBridge(
    {
        connect() {
            throw new Error("plain HTTP connect should not be used for HTTPS");
        },
    },
    async () => {
        await assertRejectsMessage(
            () => HttpClient.get("https://127.0.0.1/"),
            /SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE/,
        );
    },
);

await assertRejectsMessage(
    async () => HttpClient.create({ tls: "C:\\secret\\ca.pem" }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", { tls: { insecureSkipVerify: true } }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

{
    const cleartextClient = HttpClient.create({
        baseUrl: "http://127.0.0.1",
        tls: { trustStorePath: "C:\\secret\\ca.pem" },
    });
    await assertRejectsMessage(
        () => cleartextClient.request("/"),
        /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
    );
}

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", { tls: { privateKey: "secret" } }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", { tls: { insecureSkipVerify: "yes" } }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await assertRejectsMessage(
    async () => HttpClient.create({ tls: { trustStorePath: 123 } }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await withNetBridge(
    {
        connect() {
            throw new Error("plain HTTP connect should not be used for TLS capability checks");
        },
        connectTls() {
            throw new Error("TLS bridge should not dial unsupported TLS options");
        },
    },
    async () => {
        const cases = [
            { caPath: "C:\\secret\\ca.pem" },
            { caBundlePath: "C:\\secret\\bundle.pem" },
            { trustStorePath: "C:\\secret\\trust.pem" },
            { clientCertificatePath: "C:\\secret\\client.crt" },
            { clientPrivateKeyPath: "C:\\secret\\client.key" },
            { clientPrivateKeyPassphrase: "do-not-use" },
            { insecureSkipVerify: true },
        ];
        for (const tlsOptions of cases) {
            await assertRejectsMessage(
                () => HttpClient.get("https://127.0.0.1/", { tls: tlsOptions }),
                /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
            );
        }
    },
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

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", "not options"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

assert.throws(
    () => HttpClient.create("not options"),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
);

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", { headers: { "x-test": "a\0b" } }),
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
    const longHeader = "a".repeat(64);
    const server = await startHttpServer(() => `HTTP/1.1 200 OK\r\nX-Long: ${longHeader}\r\nContent-Length: 0\r\n\r\n`);
    try {
        await assertRejectsMessage(
            () => HttpClient.get(`${server.url}/too-many-headers`, { maxHeaderBytes: 32 }),
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
    const body = "x".repeat(1024);
    const server = await startHttpServer(() => `HTTP/1.1 200 OK\r\nContent-Length: ${body.length}\r\n\r\n${body}`);
    try {
        assert.equal(await HttpClient.text(`${server.url}/size-string`, { maxResponseBytes: "1kb" }), body);
    } finally {
        await server.close();
    }
});

await assertRejectsMessage(
    () => HttpClient.get("http://127.0.0.1/", { maxResponseBytes: "4 parsecs" }),
    /SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS/,
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
