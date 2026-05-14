import { Text } from "../codec.js";
import { HttpClient } from "../net.js";
import { EventEmitter } from "./events.js";

function toBytes(value, encoding = "utf8") {
    if (value === undefined || value === null) {
        return new Uint8Array(0);
    }
    if (typeof value === "string") {
        if (encoding !== "utf8" && encoding !== "utf-8") {
            throw new TypeError("SLOPPY_E_NODE_HTTP_UNSUPPORTED: node:http request bodies only support utf8 string encoding.");
        }
        return Text.utf8.encode(value);
    }
    if (value instanceof Uint8Array) {
        return value;
    }
    if (value instanceof ArrayBuffer) {
        return new Uint8Array(value);
    }
    if (ArrayBuffer.isView(value)) {
        return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    }
    throw new TypeError("ERR_INVALID_ARG_TYPE: node:http request body must be a string or bytes.");
}

function appendBytes(chunks) {
    const length = chunks.reduce((total, chunk) => total + chunk.byteLength, 0);
    const out = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
        out.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return out;
}

function headersObject(headers) {
    const out = Object.create(null);
    for (const [name, value] of headers.entries()) {
        const key = name.toLowerCase();
        out[key] = out[key] === undefined ? value : `${out[key]}, ${value}`;
    }
    return out;
}

function rawHeaders(headers) {
    const out = [];
    for (const [name, value] of headers.entries()) {
        out.push(name, value);
    }
    return out;
}

function normalizeRequest(protocol, input, options = undefined) {
    let url;
    let opts = {};
    if (input instanceof URL) {
        url = new URL(input.href);
    } else if (typeof input === "string") {
        url = new URL(input);
    } else if (input && typeof input === "object") {
        opts = input;
    } else {
        throw new TypeError("ERR_INVALID_ARG_TYPE: node:http.request requires a URL string, URL, or options object.");
    }
    if (typeof options === "string") {
        opts = { ...opts, path: options };
    } else if (typeof options === "object" && options !== null) {
        opts = { ...opts, ...options };
    }
    if (url === undefined) {
        const scheme = opts.protocol ?? protocol;
        const hostname = opts.hostname ?? opts.host ?? "localhost";
        const port = opts.port === undefined ? "" : `:${opts.port}`;
        const path = opts.path ?? "/";
        url = new URL(`${scheme}//${hostname}${port}${path}`);
    }
    if (url.protocol !== protocol) {
        throw new TypeError(`ERR_INVALID_PROTOCOL: expected ${protocol} request URL.`);
    }
    const headers = {};
    for (const [name, value] of Object.entries(opts.headers ?? {})) {
        headers[name] = Array.isArray(value) ? value.join(", ") : String(value);
    }
    return {
        url: url.href,
        method: String(opts.method ?? "GET").toUpperCase(),
        headers,
        timeoutMs: opts.timeout === undefined ? undefined : opts.timeout,
        signal: opts.signal,
    };
}

class IncomingMessage extends EventEmitter {
    constructor(response, body) {
        super();
        this.statusCode = response.status;
        this.statusMessage = response.statusText;
        this.headers = headersObject(response.headers);
        this.rawHeaders = rawHeaders(response.headers);
        this._body = body;
        this.readableEnded = false;
    }

    _start() {
        Promise.resolve().then(() => {
            if (this._body.byteLength > 0) {
                this.emit("data", this._body);
            }
            this.readableEnded = true;
            this.emit("end");
            this.emit("close");
        });
    }

    async *[Symbol.asyncIterator]() {
        if (this._body.byteLength > 0) {
            yield this._body;
        }
    }
}

class ClientRequest extends EventEmitter {
    constructor(protocol, input, options, callback) {
        super();
        this._request = normalizeRequest(protocol, input, options);
        this._headers = new Map(Object.entries(this._request.headers).map(([name, value]) => [name.toLowerCase(), { name, value }]));
        this._chunks = [];
        this.destroyed = false;
        this.writableEnded = false;
        if (typeof callback === "function") {
            this.once("response", callback);
        }
    }

    setHeader(name, value) {
        this._headers.set(String(name).toLowerCase(), { name: String(name), value: String(value) });
        return this;
    }

    getHeader(name) {
        return this._headers.get(String(name).toLowerCase())?.value;
    }

    removeHeader(name) {
        this._headers.delete(String(name).toLowerCase());
    }

    write(chunk, encodingOrCallback = undefined, callback = undefined) {
        if (this.destroyed || this.writableEnded) {
            throw new Error("ERR_STREAM_WRITE_AFTER_END: cannot write after request end.");
        }
        const encoding = typeof encodingOrCallback === "string" ? encodingOrCallback : "utf8";
        this._chunks.push(toBytes(chunk, encoding));
        const done = typeof encodingOrCallback === "function" ? encodingOrCallback : callback;
        if (typeof done === "function") {
            Promise.resolve().then(done);
        }
        return true;
    }

    end(chunk = undefined, encodingOrCallback = undefined, callback = undefined) {
        if (typeof chunk === "function") {
            callback = chunk;
            chunk = undefined;
        } else if (typeof encodingOrCallback === "function") {
            callback = encodingOrCallback;
        }
        if (chunk !== undefined) {
            this.write(chunk, typeof encodingOrCallback === "string" ? encodingOrCallback : "utf8");
        }
        if (typeof callback === "function") {
            this.once("finish", callback);
        }
        this.writableEnded = true;
        this.emit("finish");
        this._send();
        return this;
    }

    destroy(error = undefined) {
        this.destroyed = true;
        if (error !== undefined) {
            this.emit("error", error);
        }
        this.emit("close");
        return this;
    }

    abort() {
        return this.destroy();
    }

    async _send() {
        if (this.destroyed) {
            return;
        }
        try {
            const headers = {};
            for (const { name, value } of this._headers.values()) {
                headers[name] = value;
            }
            const response = await HttpClient.request(this._request.url, {
                method: this._request.method,
                headers,
                bytes: appendBytes(this._chunks),
                redirects: false,
                signal: this._request.signal,
                timeoutMs: this._request.timeoutMs,
            });
            const body = await response.bytes();
            if (this.destroyed) {
                return;
            }
            const message = new IncomingMessage(response, body);
            this.emit("response", message);
            message._start();
            this.emit("close");
        } catch (error) {
            if (!this.destroyed) {
                this.emit("error", error);
                this.emit("close");
            }
        }
    }
}

class Agent {
    constructor(options = undefined) {
        this.options = Object.freeze({ ...(options ?? {}) });
    }

    destroy() {}
}

const globalAgent = new Agent({ keepAlive: false });

function request(input, options = undefined, callback = undefined) {
    if (typeof options === "function") {
        callback = options;
        options = undefined;
    }
    return new ClientRequest("http:", input, options, callback);
}

function get(input, options = undefined, callback = undefined) {
    const req = request(input, options, callback);
    req.end();
    return req;
}

export { Agent, ClientRequest, IncomingMessage, get, globalAgent, request };
export default { Agent, ClientRequest, IncomingMessage, get, globalAgent, request };
