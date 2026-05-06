class SloppyNetError extends Error {
    constructor(name, message, options) {
        super(message);
        this.name = name;
        if (options?.cause !== undefined) {
            this.cause = options.cause;
        }
    }
}

class NetworkAddress {
    constructor(host, port) {
        if (typeof host !== "string" || host.length === 0) {
            throw new TypeError("NetworkAddress host must be a non-empty string.");
        }
        if (!Number.isInteger(port) || port < 0 || port > 65535) {
            throw new TypeError("NetworkAddress port must be an integer from 0 to 65535.");
        }
        this.host = host;
        this.port = port;
        Object.freeze(this);
    }

    static parse(value) {
        if (value instanceof NetworkAddress) {
            return value;
        }
        if (typeof value === "string") {
            if (value.startsWith("[")) {
                const end = value.indexOf("]");
                if (end < 0 || value[end + 1] !== ":") {
                    throw new TypeError("NetworkAddress IPv6 text must be [host]:port.");
                }
                return new NetworkAddress(
                    value.slice(1, end),
                    parsePortText(value.slice(end + 2)),
                );
            }
            const firstColon = value.indexOf(":");
            const lastColon = value.lastIndexOf(":");
            if (firstColon <= 0 || firstColon !== lastColon || lastColon === value.length - 1) {
                throw new TypeError("NetworkAddress text must be host:port or [ipv6]:port.");
            }
            return new NetworkAddress(
                value.slice(0, lastColon),
                parsePortText(value.slice(lastColon + 1)),
            );
        }
        if (typeof value !== "object" || value === null) {
            throw new TypeError("NetworkAddress.parse requires an address object.");
        }
        return new NetworkAddress(value.host, value.port);
    }

    toString() {
        return this.host.includes(":") ? `[${this.host}]:${this.port}` : `${this.host}:${this.port}`;
    }
}

function requireNetBridge() {
    const bridge = globalThis.__sloppy?.net;

    if (bridge === undefined) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.net is inactive or unavailable

Feature:
  stdlib.net

Reason:
  The active Sloppy Plan did not enable the __sloppy.net V8 intrinsic namespace.`);
    }

    return bridge;
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function normalizePort(port, allowZero) {
    const minimum = allowZero ? 0 : 1;
    if (!Number.isInteger(port) || port < minimum || port > 65535) {
        throw new TypeError(`TCP port must be an integer from ${minimum} to 65535.`);
    }
    return port;
}

function parsePortText(text) {
    if (typeof text !== "string" || text.length === 0 || !/^[0-9]+$/.test(text)) {
        throw new TypeError("TCP port text must contain decimal digits.");
    }
    return normalizePort(Number(text), true);
}

function normalizeConnectOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("TcpClient.connect options must be a plain object.");
    }
    if (typeof options.host !== "string" || options.host.length === 0) {
        throw new TypeError("TcpClient.connect host must be a non-empty string.");
    }
    const normalized = {
        host: options.host,
        port: normalizePort(options.port, false),
        noDelay: options.noDelay === true,
    };
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError("TcpClient.connect timeoutMs must be a non-negative number.");
        }
        normalized.timeoutMs = Math.ceil(options.timeoutMs);
    }
    if (options.keepAlive !== undefined) {
        if (!isPlainObject(options.keepAlive) || typeof options.keepAlive.enabled !== "boolean") {
            throw new TypeError("TcpClient.connect keepAlive must be { enabled, delayMs? }.");
        }
        normalized.keepAlive = { enabled: options.keepAlive.enabled };
        if (options.keepAlive.delayMs !== undefined) {
            if (!Number.isFinite(options.keepAlive.delayMs) || options.keepAlive.delayMs < 0) {
                throw new TypeError("TcpClient.connect keepAlive.delayMs must be non-negative.");
            }
            normalized.keepAlive.delayMs = Math.ceil(options.keepAlive.delayMs);
        }
    }
    return normalized;
}

function normalizeListenOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("TcpListener.listen options must be a plain object.");
    }
    if (typeof options.host !== "string" || options.host.length === 0) {
        throw new TypeError("TcpListener.listen host must be a non-empty string.");
    }
    const normalized = {
        host: options.host,
        port: normalizePort(options.port, true),
    };
    if (options.backlog !== undefined) {
        if (!Number.isInteger(options.backlog) || options.backlog < 1) {
            throw new TypeError("TcpListener.listen backlog must be a positive integer.");
        }
        normalized.backlog = options.backlog;
    }
    return normalized;
}

function normalizeLocalEndpointPath(path, operation) {
    if (typeof path !== "string" || path.length === 0) {
        throw new TypeError(`${operation} path must be a non-empty string.`);
    }
    if (path.includes("\0")) {
        throw new TypeError(`${operation} path must not contain NUL bytes.`);
    }
    if (/^[A-Za-z]:[\\/]/.test(path) || path.startsWith("/") || path.startsWith("\\\\")) {
        throw new TypeError(`${operation} path must use a configured named root such as runtime:/.`);
    }
    const separator = path.indexOf(":/");
    if (separator <= 0) {
        throw new TypeError(`${operation} path must use a named-root path such as runtime:/my-app.sock.`);
    }
    const root = path.slice(0, separator);
    const rest = path.slice(separator + 2);
    if (!/^[A-Za-z][A-Za-z0-9_.-]*$/.test(root)) {
        throw new TypeError(`${operation} named root is invalid.`);
    }
    if (rest.length === 0 || rest.startsWith("/") || rest.includes("\\") || rest.split("/").includes("..")) {
        throw new TypeError(`${operation} path must stay inside the named root.`);
    }
    return path;
}

function normalizePermissionMode(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (typeof value !== "string" || !/^0[0-7]{3}$/.test(value)) {
        throw new TypeError(`${operation} permissions must be an octal string such as "0600".`);
    }
    return value;
}

function normalizeLocalConnectOptions(options, operation) {
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    const normalized = { path: normalizeLocalEndpointPath(options.path, operation) };
    if (options.backend !== undefined) {
        if (options.backend !== "unix" && options.backend !== "namedPipe") {
            throw new TypeError(`${operation} backend must be "unix" or "namedPipe" when specified.`);
        }
        normalized.backend = options.backend;
    }
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError(`${operation} timeoutMs must be a non-negative number.`);
        }
        normalized.timeoutMs = Math.ceil(options.timeoutMs);
    }
    return normalized;
}

function normalizeLocalListenOptions(options, operation) {
    const normalized = normalizeLocalConnectOptions(options, operation);
    if (options.unlinkExisting !== undefined && typeof options.unlinkExisting !== "boolean") {
        throw new TypeError(`${operation} unlinkExisting must be a boolean.`);
    }
    if (options.backlog !== undefined && (!Number.isInteger(options.backlog) || options.backlog < 1)) {
        throw new TypeError(`${operation} backlog must be a positive integer.`);
    }
    normalized.unlinkExisting = options.unlinkExisting === true;
    normalized.permissions = normalizePermissionMode(options.permissions, operation);
    if (options.backlog !== undefined) {
        normalized.backlog = options.backlog;
    }
    return normalized;
}

function localIpcUnavailable() {
    throw new SloppyNetError(
        "LocalIpcFeatureUnavailableError",
        "SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE: local IPC is specified but no Unix socket or named pipe backend is active.",
    );
}

class LocalEndpointServer {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async accept(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint server is closed.");
        }
        const timeoutMs = normalizeTimeoutOption(options, "LocalEndpoint.accept");
        if (typeof this._bridge.acceptLocal !== "function") {
            localIpcUnavailable();
        }
        return this._bridge.acceptLocal(this._handle, timeoutMs);
    }

    acceptLoop(options = undefined) {
        const server = this;
        return {
            async *[Symbol.asyncIterator]() {
                while (!server.closed) {
                    yield await server.accept(options);
                }
            },
        };
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.closeLocalServer === "function") {
            await this._bridge.closeLocalServer(this._handle);
        }
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.abortLocalServer === "function") {
            await this._bridge.abortLocalServer(this._handle);
        }
    }
}

const LocalEndpoint = Object.freeze({
    async connect(options) {
        const bridge = requireNetBridge();
        const normalized = normalizeLocalConnectOptions(options, "LocalEndpoint.connect");
        if (typeof bridge.connectLocal !== "function") {
            localIpcUnavailable();
        }
        return bridge.connectLocal(normalized);
    },

    async listen(options) {
        const bridge = requireNetBridge();
        const normalized = normalizeLocalListenOptions(options, "LocalEndpoint.listen");
        if (typeof bridge.listenLocal !== "function") {
            localIpcUnavailable();
        }
        return new LocalEndpointServer(bridge, await bridge.listenLocal(normalized));
    },
});

const UnixSocket = Object.freeze({
    async connect(options) {
        return LocalEndpoint.connect({ ...options, backend: "unix" });
    },

    async listen(options) {
        return LocalEndpoint.listen({ ...options, backend: "unix" });
    },
});

const NamedPipe = Object.freeze({
    async connect(options) {
        return LocalEndpoint.connect({ ...options, backend: "namedPipe" });
    },

    async listen(options) {
        return LocalEndpoint.listen({ ...options, backend: "namedPipe" });
    },
});

function normalizeTimeoutOption(options, operation) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    for (const key of Object.keys(options)) {
        if (key !== "timeoutMs") {
            throw new TypeError(`${operation} option '${key}' is not supported.`);
        }
    }
    if (options.timeoutMs === undefined) {
        return undefined;
    }
    if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
        throw new TypeError(`${operation} timeoutMs must be a non-negative number.`);
    }
    return Math.ceil(options.timeoutMs);
}

class TcpConnection {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async write(bytes) {
        if (this._closed) {
            throw new SloppyNetError("ConnectionClosedError", "TCP connection is closed.");
        }
        if (!(bytes instanceof Uint8Array)) {
            throw new TypeError("TcpConnection.write requires a Uint8Array.");
        }
        await this._bridge.write(this._handle, bytes);
    }

    async writeText(text) {
        if (typeof text !== "string") {
            throw new TypeError("TcpConnection.writeText requires a string.");
        }
        await this.write(new TextEncoder().encode(text));
    }

    async read(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("ConnectionClosedError", "TCP connection is closed.");
        }
        const maxBytes = options?.maxBytes;
        if (maxBytes !== undefined && (!Number.isInteger(maxBytes) || maxBytes < 1)) {
            throw new TypeError("TcpConnection.read maxBytes must be a positive integer.");
        }
        return this._bridge.read(this._handle, maxBytes ?? 8192);
    }

    async readUntil(delimiter, options = undefined) {
        const delimiterBytes =
            typeof delimiter === "string" ? new TextEncoder().encode(delimiter) : delimiter;
        if (!(delimiterBytes instanceof Uint8Array) || delimiterBytes.byteLength === 0) {
            throw new TypeError("TcpConnection.readUntil delimiter must be non-empty bytes.");
        }
        const maxBytes = options?.maxBytes ?? 8192;
        return this._bridge.readUntil(this._handle, delimiterBytes, maxBytes);
    }

    async readLine(options = undefined) {
        return this._bridge.readLine(this._handle, options?.maxBytes ?? 8192);
    }

    async *readChunks(options = undefined) {
        const maxBytes = options?.maxBytes ?? 8192;
        while (!this._closed) {
            yield await this.read({ maxBytes });
        }
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.close(this._handle);
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.abort(this._handle);
    }
}

const TcpClient = Object.freeze({
    async connect(options) {
        const bridge = requireNetBridge();
        const handle = await bridge.connect(normalizeConnectOptions(options));
        return new TcpConnection(bridge, handle);
    },
});

class TcpListenerResource {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async accept(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("ListenerClosedError", "TCP listener is closed.");
        }
        const timeoutMs = normalizeTimeoutOption(options, "TcpListener.accept");
        const handle = await this._bridge.accept(this._handle, timeoutMs);
        return new TcpConnection(this._bridge, handle);
    }

    async *[Symbol.asyncIterator]() {
        while (!this._closed) {
            yield await this.accept();
        }
    }

    acceptLoop(options = undefined) {
        const listener = this;
        return {
            async *[Symbol.asyncIterator]() {
                while (!listener.closed) {
                    yield await listener.accept(options);
                }
            },
        };
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.closeListener(this._handle);
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.abortListener(this._handle);
    }
}

const TcpListener = Object.freeze({
    async listen(options) {
        const bridge = requireNetBridge();
        const handle = await bridge.listen(normalizeListenOptions(options));
        return new TcpListenerResource(bridge, handle);
    },
});

const HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES = 16 * 1024;
const HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES = 1024 * 1024;

function httpClientError(name, code, message, options = undefined) {
    return new SloppyNetError(name, `${code}: ${message}`, options);
}

function httpClientUnavailable(operation) {
    return Promise.reject(
        httpClientError(
            "HttpClientUnavailableError",
            "SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE",
            `HttpClient.${operation} requires the outbound HTTP client transport lane.`,
        ),
    );
}

function parseHttpSize(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (Number.isInteger(value) && value >= 0) {
        return value;
    }
    throw httpClientError(
        "HttpClientInvalidOptionsError",
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
        `${operation} size option must be a non-negative integer.`,
    );
}

function parseHttpPort(text, operation) {
    if (text === undefined || text === "") {
        return undefined;
    }
    if (!/^[0-9]+$/.test(text)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL port must contain decimal digits.`,
        );
    }
    const port = Number(text);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL port must be from 1 to 65535.`,
        );
    }
    return port;
}

function parseAbsoluteHttpUrl(url, operation) {
    if (typeof url !== "string" || url.length === 0) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL must be a non-empty string.`,
        );
    }
    const schemeEnd = url.indexOf("://");
    if (schemeEnd <= 0) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} requires an absolute http:// URL or a baseUrl-relative path.`,
        );
    }
    const scheme = url.slice(0, schemeEnd).toLowerCase();
    if (scheme === "https") {
        throw httpClientError(
            "HttpClientTlsUnavailableError",
            "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
            "HTTPS requires the later TLS backend slice; no custom TLS fallback is available.",
        );
    }
    if (scheme !== "http") {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} supports http:// URLs in this slice.`,
        );
    }

    const rest = url.slice(schemeEnd + 3);
    let pathStart = rest.length;
    for (const marker of ["/", "?", "#"]) {
        const index = rest.indexOf(marker);
        if (index >= 0 && index < pathStart) {
            pathStart = index;
        }
    }
    const authority = rest.slice(0, pathStart);
    let target = rest.slice(pathStart);
    if (authority.length === 0 || authority.includes("@")) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL authority is invalid.`,
        );
    }

    const fragment = target.indexOf("#");
    if (fragment >= 0) {
        target = target.slice(0, fragment);
    }
    if (target.length === 0) {
        target = "/";
    } else if (target.startsWith("?")) {
        target = `/${target}`;
    } else if (!target.startsWith("/")) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL target must be an origin-form path.`,
        );
    }

    let host;
    let portText;
    let hostHeader;
    if (authority.startsWith("[")) {
        const close = authority.indexOf("]");
        if (close <= 1 || (authority.length > close + 1 && authority[close + 1] !== ":")) {
            throw httpClientError(
                "HttpClientInvalidUrlError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} IPv6 URL host must be bracketed as [host]:port.`,
            );
        }
        host = authority.slice(1, close);
        portText = authority.length > close + 1 ? authority.slice(close + 2) : undefined;
        hostHeader = `[${host}]`;
    } else {
        const firstColon = authority.indexOf(":");
        const lastColon = authority.lastIndexOf(":");
        if (firstColon !== lastColon) {
            throw httpClientError(
                "HttpClientInvalidUrlError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} unbracketed IPv6 URL hosts are ambiguous.`,
            );
        }
        host = firstColon < 0 ? authority : authority.slice(0, firstColon);
        portText = firstColon < 0 ? undefined : authority.slice(firstColon + 1);
        hostHeader = host;
    }
    if (host.length === 0 || host.includes("\0")) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL host is invalid.`,
        );
    }

    const port = parseHttpPort(portText, operation) ?? 80;
    const headerPort = port === 80 ? "" : `:${port}`;
    return Object.freeze({
        scheme,
        host,
        port,
        hostHeader: `${hostHeader}${headerPort}`,
        target,
    });
}

function resolveHttpUrl(baseOptions, requestUrl, operation) {
    if (typeof requestUrl === "string" && requestUrl.includes("://")) {
        return parseAbsoluteHttpUrl(requestUrl, operation);
    }
    const baseUrl = baseOptions?.baseUrl;
    if (typeof requestUrl !== "string" || !requestUrl.startsWith("/") || typeof baseUrl !== "string") {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} requires an absolute URL or an absolute path with client baseUrl.`,
        );
    }
    const base = parseAbsoluteHttpUrl(baseUrl, operation);
    return Object.freeze({ ...base, target: requestUrl });
}

function normalizeHttpMethod(method, operation) {
    const resolved = method === undefined ? "GET" : method;
    if (typeof resolved !== "string" || !/^[A-Za-z]+$/.test(resolved)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} method must be a token string.`,
        );
    }
    return resolved.toUpperCase();
}

function appendHttpHeaders(target, headers, operation) {
    if (headers === undefined) {
        return;
    }
    if (!isPlainObject(headers)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} headers must be a plain object.`,
        );
    }
    for (const [name, value] of Object.entries(headers)) {
        const normalizedName = String(name).toLowerCase();
        if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} header name is invalid.`,
            );
        }
        if (normalizedName === "host" || normalizedName === "connection" || normalizedName === "content-length") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} manages Host, Connection, and Content-Length headers.`,
            );
        }
        if (typeof value !== "string" || /[\r\n]/.test(value)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} header value must be a string without CR or LF.`,
            );
        }
        target.set(normalizedName, { name, value });
    }
}

function normalizeHttpBody(options, operation) {
    const sources = ["text", "bytes"].filter((key) => options?.[key] !== undefined);
    if (sources.length > 1) {
        throw httpClientError(
            "HttpClientAmbiguousBodyError",
            "SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY",
            `${operation} accepts only one request body source.`,
        );
    }
    if (sources.length === 0) {
        return new Uint8Array(0);
    }
    if (sources[0] === "text") {
        if (typeof options.text !== "string") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} text body must be a string.`,
            );
        }
        return new TextEncoder().encode(options.text);
    }
    if (!(options.bytes instanceof Uint8Array)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} bytes body must be a Uint8Array.`,
        );
    }
    return options.bytes;
}

function concatHttpBytes(chunks, totalLength) {
    const out = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
        out.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return out;
}

function httpBytesToAscii(bytes) {
    let text = "";
    for (const byte of bytes) {
        text += String.fromCharCode(byte);
    }
    return text;
}

function findHttpHeaderEnd(bytes) {
    for (let index = 3; index < bytes.byteLength; index += 1) {
        if (bytes[index - 3] === 13 && bytes[index - 2] === 10 && bytes[index - 1] === 13 && bytes[index] === 10) {
            return index + 1;
        }
    }
    return -1;
}

class HttpHeaderBag {
    constructor(entries) {
        this._entries = entries;
        this._map = new Map(entries.map(([name, value]) => [name.toLowerCase(), value]));
        Object.freeze(this);
    }

    get(name) {
        if (typeof name !== "string") {
            throw new TypeError("HttpResponse.headers.get name must be a string.");
        }
        return this._map.get(name.toLowerCase()) ?? null;
    }

    entries() {
        return this._entries.map(([name, value]) => [name, value]);
    }
}

class HttpClientResponse {
    constructor(status, statusText, headers, body) {
        this.status = status;
        this.statusText = statusText;
        this.headers = headers;
        this._body = body;
        this._consumed = false;
    }

    _consume() {
        if (this._consumed) {
            throw httpClientError(
                "HttpClientBodyConsumedError",
                "SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED",
                "HTTP response body was already consumed.",
            );
        }
        this._consumed = true;
        return this._body;
    }

    async bytes() {
        return this._consume().slice();
    }

    async text() {
        return new TextDecoder().decode(this._consume());
    }

    async json() {
        return JSON.parse(new TextDecoder().decode(this._consume()));
    }
}

function parseHttpResponse(headBytes, bodyBytes, maxResponseBytes, complete = false) {
    const head = httpBytesToAscii(headBytes);
    const lines = head.split("\r\n");
    const statusLine = lines.shift();
    const match = /^HTTP\/1\.[01] ([0-9]{3})(?: (.*))?$/.exec(statusLine ?? "");
    const headers = [];
    let contentLength = undefined;
    let transferEncoding = undefined;

    if (match === null) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP response status line is malformed.",
        );
    }
    for (const line of lines) {
        if (line.length === 0) {
            continue;
        }
        const colon = line.indexOf(":");
        if (colon <= 0) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response header is malformed.",
            );
        }
        const name = line.slice(0, colon);
        const value = line.slice(colon + 1).trimStart();
        headers.push([name, value]);
        if (name.toLowerCase() === "content-length") {
            if (!/^[0-9]+$/.test(value)) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response Content-Length is malformed.",
                );
            }
            contentLength = Number(value);
        }
        if (name.toLowerCase() === "transfer-encoding") {
            transferEncoding = value.toLowerCase();
        }
    }
    if (transferEncoding !== undefined && transferEncoding !== "identity") {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP response Transfer-Encoding is not supported in this slice.",
        );
    }
    if (contentLength !== undefined) {
        if (contentLength > maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (bodyBytes.byteLength < contentLength) {
            return undefined;
        }
        bodyBytes = bodyBytes.slice(0, contentLength);
    } else {
        if (bodyBytes.byteLength > maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (!complete) {
            return undefined;
        }
    }
    return new HttpClientResponse(Number(match[1]), match[2] ?? "", new HttpHeaderBag(headers), bodyBytes);
}

async function readHttpResponse(connection, limits) {
    const chunks = [];
    let totalLength = 0;
    let headerEnd = -1;
    let parsed = undefined;

    while (parsed === undefined) {
        let chunk;
        try {
            chunk = await connection.read({ maxBytes: 8192 });
        } catch (error) {
            if (headerEnd >= 0) {
                break;
            }
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response ended before a complete head was received.",
                { cause: error },
            );
        }
        chunks.push(chunk);
        totalLength += chunk.byteLength;
        if (totalLength > limits.maxHeaderBytes + limits.maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response exceeded the configured limit.",
            );
        }
        const received = concatHttpBytes(chunks, totalLength);
        headerEnd = findHttpHeaderEnd(received);
        if (headerEnd < 0) {
            if (totalLength > limits.maxHeaderBytes) {
                throw httpClientError(
                    "HttpClientHeaderLimitError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response headers exceeded the configured limit.",
                );
            }
            continue;
        }
            parsed = parseHttpResponse(
                received.slice(0, headerEnd),
                received.slice(headerEnd),
                limits.maxResponseBytes,
            );
        }
        if (parsed === undefined) {
            const received = concatHttpBytes(chunks, totalLength);
            parsed = parseHttpResponse(
                received.slice(0, headerEnd),
                received.slice(headerEnd),
                limits.maxResponseBytes,
                true,
            );
        }
    return parsed;
}

function normalizeHttpRequest(baseOptions, request, options, defaultMethod) {
    const operation = "HttpClient.request";
    const requestObject = typeof request === "string" ? { ...(options ?? {}), url: request } : request;
    if (!isPlainObject(requestObject)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            "HttpClient.request requires a request object or URL string.",
        );
    }
    const url = resolveHttpUrl(baseOptions, requestObject.url, operation);
    const method = normalizeHttpMethod(defaultMethod ?? requestObject.method, operation);
    const headers = new Map();
    appendHttpHeaders(headers, baseOptions?.headers, operation);
    appendHttpHeaders(headers, requestObject.headers, operation);
    const body = normalizeHttpBody(requestObject, operation);
    const maxResponseBytes =
        parseHttpSize(requestObject.maxResponseBytes ?? baseOptions?.maxResponseBytes, operation) ??
        HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES;
    const maxHeaderBytes =
        parseHttpSize(requestObject.maxHeaderBytes ?? baseOptions?.maxHeaderBytes, operation) ??
        HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES;
    const timeoutMs = requestObject.timeoutMs ?? baseOptions?.timeoutMs;
    if (timeoutMs !== undefined && (!Number.isFinite(timeoutMs) || timeoutMs < 0)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} timeoutMs must be a non-negative number.`,
        );
    }
    return Object.freeze({
        url,
        method,
        headers,
        body,
        timeoutMs: timeoutMs === undefined ? undefined : Math.ceil(timeoutMs),
        maxHeaderBytes,
        maxResponseBytes,
    });
}

function serializeHttpRequest(request) {
    const lines = [
        `${request.method} ${request.url.target} HTTP/1.1`,
        `Host: ${request.url.hostHeader}`,
        "Connection: close",
        "Accept: */*",
    ];
    for (const { name, value } of request.headers.values()) {
        lines.push(`${name}: ${value}`);
    }
    if (request.body.byteLength > 0) {
        lines.push(`Content-Length: ${request.body.byteLength}`);
    }
    lines.push("", "");
    const head = new TextEncoder().encode(lines.join("\r\n"));
    const bytes = new Uint8Array(head.byteLength + request.body.byteLength);
    bytes.set(head, 0);
    bytes.set(request.body, head.byteLength);
    return bytes;
}

function mapHttpTransportError(error) {
    const message = String(error?.message ?? error);
    if (message.includes("SLOPPY_E_NET_DNS_FAILURE")) {
        return httpClientError(
            "HttpClientDnsError",
            "SLOPPY_E_HTTP_CLIENT_DNS_FAILED",
            "HTTP client DNS resolution failed.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_NET_CONNECT_TIMEOUT")) {
        return httpClientError(
            "HttpClientTimeoutError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
            "HTTP client request timed out.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_NET_CONNECT_CANCELLED")) {
        return httpClientError(
            "HttpClientCancelledError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
            "HTTP client request was cancelled.",
            { cause: error },
        );
    }
    return httpClientError(
        "HttpClientConnectError",
        "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
        "HTTP client transport operation failed.",
        { cause: error },
    );
}

async function sendHttpRequest(baseOptions, request, options = undefined, defaultMethod = undefined) {
    let connection;
    const normalized = normalizeHttpRequest(baseOptions, request, options, defaultMethod);
    try {
        if (typeof globalThis.__sloppy?.net?.connect !== "function") {
            return await httpClientUnavailable("request");
        }
        connection = await TcpClient.connect({
            host: normalized.url.host,
            port: normalized.url.port,
            timeoutMs: normalized.timeoutMs,
            noDelay: true,
        });
        await connection.write(serializeHttpRequest(normalized));
        return await readHttpResponse(connection, normalized);
    } catch (error) {
        if (error instanceof SloppyNetError && String(error.message).startsWith("SLOPPY_E_HTTP_CLIENT_")) {
            throw error;
        }
        throw mapHttpTransportError(error);
    } finally {
        if (connection !== undefined) {
            await connection.close().catch(() => {});
        }
    }
}

function createHttpClientFacade(baseOptions = undefined) {
    const client = {
        request(request, options = undefined) {
            return sendHttpRequest(baseOptions, request, options);
        },
        get(url, options = undefined) {
            return sendHttpRequest(baseOptions, url, options, "GET");
        },
        post(url, options = undefined) {
            return sendHttpRequest(baseOptions, url, options, "POST");
        },
        getJson() {
            return httpClientUnavailable("getJson");
        },
        postJson() {
            return httpClientUnavailable("postJson");
        },
    };
    Object.defineProperty(client, "__sloppyHttpClientOptions", {
        value: baseOptions,
        enumerable: false,
    });
    return Object.freeze(client);
}

const HttpClient = Object.freeze({
    create(options = undefined) {
        return createHttpClientFacade(options);
    },
    request(request, options = undefined) {
        return sendHttpRequest(undefined, request, options);
    },
    get(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "GET");
    },
    post(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "POST");
    },
    getJson() {
        return httpClientUnavailable("getJson");
    },
    postJson() {
        return httpClientUnavailable("postJson");
    },
});

export {
    HttpClient,
    LocalEndpoint,
    NamedPipe,
    NetworkAddress,
    SloppyNetError,
    TcpClient,
    TcpConnection,
    TcpListener,
    UnixSocket,
};
