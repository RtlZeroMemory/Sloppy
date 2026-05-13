import { Text } from "../codec.js";
import { createHeaderLookup } from "./headers.js";
import { loopbackAuthority, reserveLoopbackPort } from "./testhost-loopback.js";

const PROBLEM_CONTENT_TYPE = "application/problem+json; charset=utf-8";

function statusText(status) {
    const titles = {
        200: "OK",
        201: "Created",
        202: "Accepted",
        204: "No Content",
        400: "Bad Request",
        404: "Not Found",
        500: "Internal Server Error",
        503: "Service Unavailable",
    };
    return titles[status] ?? "Mock Response";
}

function concatBytes(parts) {
    const total = parts.reduce((sum, part) => sum + part.byteLength, 0);
    const result = new Uint8Array(total);
    let offset = 0;
    for (const part of parts) {
        result.set(part, offset);
        offset += part.byteLength;
    }
    return result;
}

function indexOfBytes(buffer, needle) {
    outer:
    for (let index = 0; index <= buffer.byteLength - needle.byteLength; index += 1) {
        for (let offset = 0; offset < needle.byteLength; offset += 1) {
            if (buffer[index + offset] !== needle[offset]) {
                continue outer;
            }
        }
        return index;
    }
    return -1;
}

async function readMockServerRequest(connection, options = {}) {
    const chunks = [];
    const headerEndBytes = Text.utf8.encode("\r\n\r\n");
    const maxHeaderBytes = options.maxHeaderBytes ?? 64 * 1024;
    let combined = new Uint8Array(0);
    let headerEnd = -1;
    while (headerEnd < 0) {
        const chunk = await connection.read(8192);
        if (chunk.byteLength === 0) {
            throw new Error("Mock HTTP server received a closed connection before request headers.");
        }
        chunks.push(chunk);
        combined = concatBytes(chunks);
        if (combined.byteLength > maxHeaderBytes) {
            throw new Error("Mock HTTP server request headers exceeded the configured limit.");
        }
        headerEnd = indexOfBytes(combined, headerEndBytes);
    }
    const head = Text.utf8.decode(combined.slice(0, headerEnd));
    const lines = head.split("\r\n");
    const requestLine = lines.shift() ?? "";
    const requestMatch = /^([A-Z]+)\s+(\S+)\s+HTTP\/1\.[01]$/u.exec(requestLine);
    if (requestMatch === null) {
        throw new Error(`Mock HTTP server received an invalid request line: ${requestLine}`);
    }
    const headers = {};
    for (const line of lines) {
        const colon = line.indexOf(":");
        if (colon > 0) {
            headers[line.slice(0, colon).trim()] = line.slice(colon + 1).trim();
        }
    }
    const headerLookup = createHeaderLookup(headers);
    const contentLengthText = headerLookup.get("content-length") ?? "0";
    if (!/^[0-9]+$/u.test(contentLengthText)) {
        throw new Error("Mock HTTP server received an invalid Content-Length header.");
    }
    const contentLength = Number(contentLengthText);
    const maxBodyBytes = options.maxBodyBytes ?? 4 * 1024 * 1024;
    if (!Number.isSafeInteger(contentLength) || contentLength > maxBodyBytes) {
        throw new Error("Mock HTTP server request body exceeded the configured limit.");
    }
    const bodyChunks = [];
    const firstBodyChunk = combined.slice(headerEnd + headerEndBytes.byteLength);
    let bodyBytesRead = Math.min(firstBodyChunk.byteLength, contentLength);
    if (bodyBytesRead > 0) {
        bodyChunks.push(firstBodyChunk.slice(0, bodyBytesRead));
    }
    while (bodyBytesRead < contentLength) {
        const chunk = await connection.read(Math.min(8192, contentLength - bodyBytesRead));
        if (chunk.byteLength === 0) {
            throw new Error("Mock HTTP server connection closed before the request body finished.");
        }
        bodyChunks.push(chunk);
        bodyBytesRead += chunk.byteLength;
    }
    const body = concatBytes(bodyChunks).slice(0, contentLength);
    const contentType = headerLookup.get("content-type") ?? "";
    const text = body.byteLength === 0 ? undefined : Text.utf8.decode(body);
    const mediaType = contentType.split(";", 1)[0].trim().toLowerCase();
    let json;
    if (
        text !== undefined &&
        (mediaType === "application/json" || (mediaType.startsWith("application/") && mediaType.endsWith("+json")))
    ) {
        json = JSON.parse(text);
    }
    return Object.freeze({
        method: requestMatch[1],
        url: requestMatch[2],
        headers,
        json,
        text,
        bytes: body.byteLength === 0 ? undefined : body,
    });
}

async function writeMockServerResponse(connection, response) {
    const headers = { ...response.headers };
    const headerLookup = createHeaderLookup(headers);
    if (headerLookup.get("content-length") === undefined) {
        headers["content-length"] = String(response.body.byteLength);
    }
    if (headerLookup.get("connection") === undefined) {
        headers.connection = "close";
    }
    const head = [
        `HTTP/1.1 ${response.status} ${statusText(response.status)}`,
        ...Object.entries(headers).map(([name, value]) => `${name}: ${value}`),
        "",
        "",
    ].join("\r\n");
    await connection.write(Text.utf8.encode(head));
    if (response.body.byteLength !== 0) {
        await connection.write(response.body);
    }
}

export async function startHttpMockServer(name, mock, options = {}) {
    const host = options.httpMockHost ?? options.host ?? "127.0.0.1";
    const reservation = await reserveLoopbackPort(host, {
        portReservationAttempts: options.httpMockPortReservationAttempts ?? options.portReservationAttempts,
    });
    const listener = reservation.listener;
    const connections = new Set();
    const activeHandlers = new Set();
    const timeoutWaiters = new Set();
    let closed = false;

    function waitForMockTimeout() {
        if (closed) {
            return Promise.resolve();
        }
        return new Promise((resolve) => {
            let wake;
            const timer = setTimeout(() => {
                timeoutWaiters.delete(wake);
                resolve();
            }, options.httpMockTimeoutHoldMs ?? 60000);
            wake = () => {
                clearTimeout(timer);
                resolve();
            };
            timeoutWaiters.add(wake);
        });
    }

    async function handleConnection(connection) {
        connections.add(connection);
        try {
            const request = await readMockServerRequest(connection, options);
            const response = await mock._dispatchRaw(request);
            await writeMockServerResponse(connection, response);
        } catch (error) {
            if (closed || error?.code === "SLOPPY_E_HTTP_TIMEOUT") {
                await waitForMockTimeout();
                return;
            }
            const body = Text.utf8.encode(JSON.stringify({
                code: error?.code ?? "SLOPPY_E_HTTP_MOCK_SERVER",
                message: "Outbound HTTP mock failed.",
            }));
            await writeMockServerResponse(connection, Object.freeze({
                status: 500,
                headers: Object.freeze({ "content-type": PROBLEM_CONTENT_TYPE }),
                body,
            })).catch(() => {});
        } finally {
            await connection.close().catch(() => {});
            connections.delete(connection);
        }
    }

    const serving = (async () => {
        while (!closed) {
            try {
                const connection = await listener.accept({ timeoutMs: 250 });
                const active = handleConnection(connection).finally(() => {
                    activeHandlers.delete(active);
                });
                activeHandlers.add(active);
            } catch (error) {
                if (closed) {
                    break;
                }
                if (/timeout/iu.test(String(error?.message ?? error))) {
                    continue;
                }
                break;
            }
        }
    })();

    return Object.freeze({
        name,
        baseUrl: `http://${loopbackAuthority(host, reservation.port)}`,
        async close() {
            if (closed) {
                return;
            }
            closed = true;
            for (const wake of [...timeoutWaiters]) {
                timeoutWaiters.delete(wake);
                wake();
            }
            await listener.abort().catch(async () => listener.close().catch(() => {}));
            for (const connection of [...connections]) {
                await connection.abort().catch(() => {});
            }
            await Promise.allSettled([...activeHandlers]);
            await serving.catch(() => {});
        },
    });
}
