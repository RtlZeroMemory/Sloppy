import { Base64, Text } from "./codec.js";
import { Random, Secret } from "./crypto.js";
import { Metrics } from "./metrics.js";
import { TcpClient, TcpConnection } from "./net.js";
import { Schema } from "./schema.js";

const ASYNC_DISPOSE = Symbol.asyncDispose;
const SECRET_REDACTION = "[REDACTED]";
const DEFAULT_PORT = 6379;
const DEFAULT_CONNECT_TIMEOUT_MS = 5000;
const DEFAULT_COMMAND_TIMEOUT_MS = 5000;
const DEFAULT_MAX_VALUE_BYTES = 1024 * 1024;
const DEFAULT_MAX_RESPONSE_BYTES = 16 * 1024 * 1024;
const DEFAULT_MAX_ARRAY_ITEMS = 1024 * 1024;
const DEFAULT_MAX_ARRAY_DEPTH = 32;
const DEFAULT_POOL_MAX_CONNECTIONS = 8;
const DEFAULT_POOL_IDLE_TIMEOUT_MS = 30000;
const DEFAULT_POOL_PENDING_LIMIT = 128;
const DEFAULT_POOL_PENDING_TIMEOUT_MS = 5000;
const COMMAND_PATTERN = /^[A-Z][A-Z0-9_]*$/u;
const NAME_PATTERN = /^[A-Za-z0-9_.-]+$/u;
const LOCK_RELEASE_SCRIPT = `
if redis.call("GET", KEYS[1]) == ARGV[1] then
  return redis.call("DEL", KEYS[1])
end
return 0`;
const LOCK_EXTEND_SCRIPT = `
if redis.call("GET", KEYS[1]) == ARGV[1] then
  return redis.call("PEXPIRE", KEYS[1], ARGV[2])
end
return 0`;

const ERROR_NAMES = Object.freeze({
    SLOPPY_E_REDIS_INVALID_OPTIONS: "SloppyRedisInvalidOptionsError",
    SLOPPY_E_REDIS_CONNECT_FAILED: "SloppyRedisConnectError",
    SLOPPY_E_REDIS_AUTH_FAILED: "SloppyRedisAuthError",
    SLOPPY_E_REDIS_COMMAND_FAILED: "SloppyRedisCommandError",
    SLOPPY_E_REDIS_PROTOCOL_ERROR: "SloppyRedisProtocolError",
    SLOPPY_E_REDIS_TIMEOUT: "SloppyRedisTimeoutError",
    SLOPPY_E_REDIS_CANCELLED: "SloppyRedisCancelledError",
    SLOPPY_E_REDIS_CLOSED: "SloppyRedisClosedError",
    SLOPPY_E_REDIS_RESPONSE_VALIDATION_FAILED: "SloppyRedisResponseValidationError",
    SLOPPY_E_REDIS_VALUE_TOO_LARGE: "SloppyRedisValueTooLargeError",
    SLOPPY_E_REDIS_LOCK_TIMEOUT: "SloppyRedisLockTimeoutError",
    SLOPPY_E_REDIS_LOCK_LOST: "SloppyRedisLockLostError",
    SLOPPY_E_REDIS_LOCK_RELEASE_FAILED: "SloppyRedisLockReleaseError",
});

class SloppyRedisError extends Error {
    constructor(code, message, options = undefined) {
        super(`${code}: ${message}`);
        this.name = ERROR_NAMES[code] ?? "SloppyRedisError";
        this.code = code;
        if (options?.cause !== undefined) {
            this.cause = options.cause;
        }
        if (options?.redisCode !== undefined) {
            this.redisCode = options.redisCode;
        }
        if (options?.command !== undefined) {
            this.command = options.command;
        }
        Object.defineProperty(this, "details", {
            value: options?.details,
            enumerable: false,
        });
    }
}

class RedisErrorReply {
    constructor(message) {
        this.message = message;
        this.code = String(message).split(/\s+/u, 1)[0] || "ERR";
        Object.freeze(this);
    }
}

function redisError(code, message, options = undefined) {
    return new SloppyRedisError(code, message, options);
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function secretToText(value) {
    if (value === undefined || value === null) {
        return undefined;
    }
    if (typeof value === "string") {
        return value;
    }
    if (typeof value.bytes === "function") {
        return Text.utf8.decode(value.bytes());
    }
    throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis password must be a string or Secret.");
}

function assertName(name, subject = "client name") {
    if (typeof name !== "string" || name.length === 0 || name.length > 128 || !NAME_PATTERN.test(name)) {
        throw redisError(
            "SLOPPY_E_REDIS_INVALID_OPTIONS",
            `Redis ${subject} must contain only letters, digits, dots, underscores, or hyphens.`,
        );
    }
}

function normalizeInteger(value, fallback, subject, min, max) {
    if (value === undefined) {
        return fallback;
    }
    if (!Number.isInteger(value) || value < min || value > max) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", `Redis ${subject} must be an integer from ${min} to ${max}.`);
    }
    return value;
}

function normalizeTimeout(value, fallback, subject) {
    return normalizeInteger(value, fallback, subject, 1, 0xffffffff);
}

function normalizeNonNegativeTimeout(value, fallback, subject) {
    return normalizeInteger(value, fallback, subject, 0, 0xffffffff);
}

function normalizePositiveBytes(value, fallback, subject) {
    return normalizeInteger(value, fallback, subject, 1, Number.MAX_SAFE_INTEGER);
}

function normalizePool(options = undefined) {
    if (options === undefined) {
        options = {};
    }
    if (!isPlainObject(options)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis pool options must be a plain object.");
    }
    return Object.freeze({
        maxConnections: normalizeInteger(options.maxConnections, DEFAULT_POOL_MAX_CONNECTIONS, "pool.maxConnections", 1, 1024),
        idleTimeoutMs: normalizeTimeout(options.idleTimeoutMs, DEFAULT_POOL_IDLE_TIMEOUT_MS, "pool.idleTimeoutMs"),
        pendingQueueLimit: normalizeInteger(options.pendingQueueLimit, DEFAULT_POOL_PENDING_LIMIT, "pool.pendingQueueLimit", 0, 100000),
        pendingQueueTimeoutMs: normalizeTimeout(options.pendingQueueTimeoutMs, DEFAULT_POOL_PENDING_TIMEOUT_MS, "pool.pendingQueueTimeoutMs"),
    });
}

function parseRedisUrl(rawUrl, options) {
    if (typeof rawUrl !== "string" || rawUrl.length === 0) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis url must be a non-empty string.");
    }
    let parsed;
    try {
        parsed = new URL(rawUrl);
    } catch (error) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis url must be a valid redis:// or rediss:// URL.", { cause: error });
    }
    if (parsed.protocol !== "redis:" && parsed.protocol !== "rediss:") {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis url must use redis:// or rediss://.");
    }
    if (parsed.hostname.length === 0) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis url must include a host.");
    }
    const pathDatabase = parsed.pathname.length > 1 ? Number(parsed.pathname.slice(1)) : undefined;
    if (pathDatabase !== undefined && (!Number.isInteger(pathDatabase) || pathDatabase < 0)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis URL database path must be a non-negative integer.");
    }
    const username = options.username ?? (parsed.username.length === 0 ? undefined : decodeURIComponent(parsed.username));
    const password = secretToText(options.password) ?? (parsed.password.length === 0 ? undefined : decodeURIComponent(parsed.password));
    const database = normalizeInteger(options.database, pathDatabase ?? 0, "database", 0, options.maxDatabase ?? 15);
    return Object.freeze({
        rawUrl,
        redactedUrl: redactRedisUrl(rawUrl),
        scheme: parsed.protocol.slice(0, -1),
        tls: options.tls ?? parsed.protocol === "rediss:",
        host: parsed.hostname,
        port: parsed.port === "" ? DEFAULT_PORT : Number(parsed.port),
        username,
        password,
        database,
    });
}

function normalizeClientOptions(name, options = {}) {
    assertName(name);
    if (!isPlainObject(options)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis client options must be a plain object.");
    }
    const endpoint = parseRedisUrl(options.url, options);
    return Object.freeze({
        name,
        endpoint,
        connectTimeoutMs: normalizeTimeout(options.connectTimeoutMs, DEFAULT_CONNECT_TIMEOUT_MS, "connectTimeoutMs"),
        commandTimeoutMs: normalizeTimeout(options.commandTimeoutMs, DEFAULT_COMMAND_TIMEOUT_MS, "commandTimeoutMs"),
        maxValueBytes: normalizePositiveBytes(options.maxValueBytes, DEFAULT_MAX_VALUE_BYTES, "maxValueBytes"),
        maxResponseBytes: normalizePositiveBytes(options.maxResponseBytes, DEFAULT_MAX_RESPONSE_BYTES, "maxResponseBytes"),
        maxArrayItems: normalizeInteger(options.maxArrayItems, DEFAULT_MAX_ARRAY_ITEMS, "maxArrayItems", 1, Number.MAX_SAFE_INTEGER),
        maxArrayDepth: normalizeInteger(options.maxArrayDepth, DEFAULT_MAX_ARRAY_DEPTH, "maxArrayDepth", 1, 256),
        pool: normalizePool(options.pool),
        validateOnConnect: options.validateOnConnect !== false,
    });
}

function redactRedisUrl(value) {
    try {
        const parsed = new URL(String(value));
        if (parsed.password.length > 0) {
            parsed.password = SECRET_REDACTION;
        }
        if (parsed.username.length > 0 && parsed.password.length > 0) {
            parsed.username = SECRET_REDACTION;
        }
        return parsed.toString();
    } catch {
        return String(value).replace(/(:\/\/[^:\s]+:)([^@\s]+)(@)/u, `$1${SECRET_REDACTION}$3`);
    }
}

function safeCommandName(name) {
    if (typeof name !== "string" || !COMMAND_PATTERN.test(name.toUpperCase())) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis command name must be a safe command token.");
    }
    return name.toUpperCase();
}

function bytesFromArgument(value, subject = "Redis command argument") {
    if (typeof value === "string") {
        return Text.utf8.encode(value);
    }
    if (typeof value === "number") {
        if (!Number.isSafeInteger(value)) {
            throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", `${subject} number must be a safe integer.`);
        }
        return Text.utf8.encode(String(value));
    }
    if (typeof value === "bigint") {
        return Text.utf8.encode(String(value));
    }
    if (value instanceof Uint8Array) {
        return value;
    }
    throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", `${subject} must be a string, bytes, number, or bigint.`);
}

function concatBytes(chunks, total) {
    const output = new Uint8Array(total);
    let offset = 0;
    for (const chunk of chunks) {
        output.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return output;
}

function encodeCommand(args) {
    if (!Array.isArray(args) || args.length === 0) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis command must contain at least a command name.");
    }
    const parts = [Text.utf8.encode(`*${args.length}\r\n`)];
    let total = parts[0].byteLength;
    for (const arg of args) {
        const bytes = bytesFromArgument(arg);
        const head = Text.utf8.encode(`$${bytes.byteLength}\r\n`);
        const tail = Text.utf8.encode("\r\n");
        parts.push(head, bytes, tail);
        total += head.byteLength + bytes.byteLength + tail.byteLength;
    }
    return concatBytes(parts, total);
}

function findCrlf(bytes, start) {
    for (let index = start; index + 1 < bytes.byteLength; index += 1) {
        if (bytes[index] === 13 && bytes[index + 1] === 10) {
            return index;
        }
    }
    return -1;
}

function ascii(bytes, start, end) {
    return Text.utf8.decode(bytes.subarray(start, end));
}

function parseIntegerText(text, subject) {
    if (!/^-?[0-9]+$/u.test(text)) {
        throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", `Redis protocol ${subject} is malformed.`);
    }
    const value = Number(text);
    if (!Number.isSafeInteger(value)) {
        throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", `Redis protocol ${subject} is outside safe integer range.`);
    }
    return value;
}

class RespParser {
    constructor(options = {}) {
        this._buffer = new Uint8Array(0);
        this._maxResponseBytes = options.maxResponseBytes ?? DEFAULT_MAX_RESPONSE_BYTES;
        this._maxArrayItems = options.maxArrayItems ?? DEFAULT_MAX_ARRAY_ITEMS;
        this._maxArrayDepth = options.maxArrayDepth ?? DEFAULT_MAX_ARRAY_DEPTH;
    }

    get bufferedBytes() {
        return this._buffer.byteLength;
    }

    feed(chunk) {
        if (!(chunk instanceof Uint8Array)) {
            throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis parser input must be bytes.");
        }
        const total = this._buffer.byteLength + chunk.byteLength;
        if (total > this._maxResponseBytes) {
            throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis response exceeded the configured byte limit.");
        }
        const next = new Uint8Array(total);
        next.set(this._buffer, 0);
        next.set(chunk, this._buffer.byteLength);
        this._buffer = next;
    }

    read() {
        const parsed = this._parseAt(0, 0);
        if (parsed === undefined) {
            return undefined;
        }
        this._buffer = this._buffer.subarray(parsed.offset);
        return parsed.value;
    }

    _parseLine(offset, subject) {
        const end = findCrlf(this._buffer, offset);
        if (end < 0) {
            return undefined;
        }
        return { text: ascii(this._buffer, offset, end), offset: end + 2, subject };
    }

    _parseAt(offset, depth) {
        if (offset >= this._buffer.byteLength) {
            return undefined;
        }
        if (depth > this._maxArrayDepth) {
            throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis response exceeded maximum array depth.");
        }
        const prefix = this._buffer[offset];
        if (prefix === 43) {
            const line = this._parseLine(offset + 1, "simple string");
            return line === undefined ? undefined : { value: line.text, offset: line.offset };
        }
        if (prefix === 45) {
            const line = this._parseLine(offset + 1, "error");
            return line === undefined ? undefined : { value: new RedisErrorReply(line.text), offset: line.offset };
        }
        if (prefix === 58) {
            const line = this._parseLine(offset + 1, "integer");
            return line === undefined ? undefined : { value: parseIntegerText(line.text, "integer"), offset: line.offset };
        }
        if (prefix === 36) {
            const line = this._parseLine(offset + 1, "bulk length");
            if (line === undefined) {
                return undefined;
            }
            const length = parseIntegerText(line.text, "bulk length");
            if (length === -1) {
                return { value: null, offset: line.offset };
            }
            if (length < 0) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis bulk length is malformed.");
            }
            if (length > this._maxResponseBytes) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis bulk response exceeded the configured byte limit.");
            }
            const end = line.offset + length;
            if (end + 2 > this._buffer.byteLength) {
                return undefined;
            }
            if (this._buffer[end] !== 13 || this._buffer[end + 1] !== 10) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis bulk response is missing CRLF terminator.");
            }
            return { value: this._buffer.slice(line.offset, end), offset: end + 2 };
        }
        if (prefix === 42) {
            const line = this._parseLine(offset + 1, "array length");
            if (line === undefined) {
                return undefined;
            }
            const length = parseIntegerText(line.text, "array length");
            if (length === -1) {
                return { value: null, offset: line.offset };
            }
            if (length < 0 || length > this._maxArrayItems) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis array length is outside configured limits.");
            }
            const values = [];
            let cursor = line.offset;
            for (let index = 0; index < length; index += 1) {
                const parsed = this._parseAt(cursor, depth + 1);
                if (parsed === undefined) {
                    return undefined;
                }
                values.push(parsed.value);
                cursor = parsed.offset;
            }
            return { value: values, offset: cursor };
        }
        throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis response prefix is not valid RESP2.");
    }
}

function replyText(reply) {
    if (reply === null || reply === undefined) {
        return undefined;
    }
    if (reply instanceof Uint8Array) {
        return Text.utf8.decode(reply);
    }
    return String(reply);
}

function assertNotErrorReply(reply, command) {
    if (reply instanceof RedisErrorReply) {
        const code = command === "AUTH" ? "SLOPPY_E_REDIS_AUTH_FAILED" : "SLOPPY_E_REDIS_COMMAND_FAILED";
        throw redisError(code, `Redis command ${command} failed: ${reply.message}`, {
            redisCode: reply.code,
            command,
        });
    }
    return reply;
}

function withTimeout(promise, timeoutMs, onTimeout) {
    let timer;
    return Promise.race([
        promise,
        new Promise((_, reject) => {
            timer = setTimeout(() => reject(onTimeout()), timeoutMs);
        }),
    ]).finally(() => clearTimeout(timer));
}

async function openConnection(options) {
    try {
        if (options.endpoint.tls) {
            const bridge = globalThis.__sloppy?.net;
            if (bridge === undefined || typeof bridge.connectTls !== "function") {
                throw redisError("SLOPPY_E_REDIS_CONNECT_FAILED", "Redis rediss:// requires the outbound TLS bridge.");
            }
            const handle = await bridge.connectTls({
                host: options.endpoint.host,
                port: options.endpoint.port,
                timeoutMs: options.connectTimeoutMs,
                noDelay: true,
                serverName: options.endpoint.host,
            });
            return new TcpConnection(bridge, handle);
        }
        return await TcpClient.connect({
            host: options.endpoint.host,
            port: options.endpoint.port,
            timeoutMs: options.connectTimeoutMs,
            noDelay: true,
        });
    } catch (error) {
        if (error instanceof SloppyRedisError) {
            throw error;
        }
        throw redisError("SLOPPY_E_REDIS_CONNECT_FAILED", "Redis connection failed.", { cause: error });
    }
}

class RedisConnection {
    constructor(connection, options, metrics) {
        this.connection = connection;
        this.options = options;
        this.parser = new RespParser(options);
        this.metrics = metrics;
        this.broken = false;
        this.closed = false;
    }

    async initialize() {
        const password = this.options.endpoint.password;
        if (password !== undefined) {
            const args = this.options.endpoint.username === undefined
                ? ["AUTH", password]
                : ["AUTH", this.options.endpoint.username, password];
            const reply = await this.command(args, { commandName: "AUTH" });
            if (replyText(reply) !== "OK") {
                throw redisError("SLOPPY_E_REDIS_AUTH_FAILED", "Redis AUTH did not return OK.");
            }
        }
        if (this.options.endpoint.database !== 0) {
            const reply = await this.command(["SELECT", this.options.endpoint.database], { commandName: "SELECT" });
            if (replyText(reply) !== "OK") {
                throw redisError("SLOPPY_E_REDIS_COMMAND_FAILED", "Redis SELECT did not return OK.", { command: "SELECT" });
            }
        }
        if (this.options.validateOnConnect) {
            await this.command(["PING"], { commandName: "PING" });
        }
    }

    async command(args, options = {}) {
        const commandName = options.commandName ?? safeCommandName(String(args[0]));
        const encoded = encodeCommand(args);
        this.metrics.bytesOut += encoded.byteLength;
        const run = async () => {
            await this.connection.write(encoded);
            while (true) {
                const reply = this.parser.read();
                if (reply !== undefined) {
                    return assertNotErrorReply(reply, commandName);
                }
                const chunk = await this.connection.read({ maxBytes: 8192 });
                if (!(chunk instanceof Uint8Array) || chunk.byteLength === 0) {
                    throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis connection closed before a complete response.");
                }
                this.metrics.bytesIn += chunk.byteLength;
                this.parser.feed(chunk);
            }
        };
        try {
            return await withTimeout(
                run(),
                options.timeoutMs ?? this.options.commandTimeoutMs,
                () => redisError("SLOPPY_E_REDIS_TIMEOUT", `Redis command ${commandName} timed out.`, { command: commandName }),
            );
        } catch (error) {
            this.broken = true;
            if (error instanceof SloppyRedisError) {
                throw error;
            }
            throw redisError("SLOPPY_E_REDIS_COMMAND_FAILED", `Redis command ${commandName} failed.`, { cause: error, command: commandName });
        }
    }

    async pipeline(commands, options = {}) {
        const encodedCommands = commands.map((command) => encodeCommand(command));
        const total = encodedCommands.reduce((sum, bytes) => sum + bytes.byteLength, 0);
        const payload = concatBytes(encodedCommands, total);
        this.metrics.bytesOut += payload.byteLength;
        const run = async () => {
            await this.connection.write(payload);
            const replies = [];
            while (replies.length < commands.length) {
                const reply = this.parser.read();
                if (reply !== undefined) {
                    replies.push(assertNotErrorReply(reply, safeCommandName(String(commands[replies.length][0]))));
                    continue;
                }
                const chunk = await this.connection.read({ maxBytes: 8192 });
                if (!(chunk instanceof Uint8Array) || chunk.byteLength === 0) {
                    throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis connection closed before a complete pipeline response.");
                }
                this.metrics.bytesIn += chunk.byteLength;
                this.parser.feed(chunk);
            }
            return replies;
        };
        try {
            return await withTimeout(
                run(),
                options.timeoutMs ?? this.options.commandTimeoutMs,
                () => redisError("SLOPPY_E_REDIS_TIMEOUT", "Redis pipeline timed out.", { command: "PIPELINE" }),
            );
        } catch (error) {
            this.broken = true;
            throw error;
        }
    }

    async close() {
        if (this.closed) {
            return;
        }
        this.closed = true;
        await this.connection.close().catch(() => {});
    }

    async abort() {
        if (this.closed) {
            return;
        }
        this.closed = true;
        await this.connection.abort().catch(() => {});
    }
}

class RedisConnectionPool {
    constructor(options, metrics) {
        this.options = options;
        this.metrics = metrics;
        this.idle = [];
        this.active = 0;
        this.queue = [];
        this.closed = false;
    }

    async _create() {
        this.active += 1;
        this.metrics.connectionsCreated += 1;
        const connection = new RedisConnection(await openConnection(this.options), this.options, this.metrics);
        try {
            await connection.initialize();
        } catch (error) {
            this.active -= 1;
            await connection.abort().catch(() => {});
            throw error;
        }
        return connection;
    }

    async acquire() {
        if (this.closed) {
            throw redisError("SLOPPY_E_REDIS_CLOSED", "Redis client is closed.");
        }
        const now = Date.now();
        while (this.idle.length > 0) {
            const entry = this.idle.pop();
            if (entry.connection.closed || entry.connection.broken || now - entry.releasedAt > this.options.pool.idleTimeoutMs) {
                this.active -= 1;
                this.metrics.connectionsClosed += 1;
                await entry.connection.close().catch(() => {});
                continue;
            }
            this.metrics.connectionsReused += 1;
            return entry.connection;
        }
        if (this.active < this.options.pool.maxConnections) {
            return await this._create();
        }
        if (this.queue.length >= this.options.pool.pendingQueueLimit) {
            this.metrics.poolRejected += 1;
            throw redisError("SLOPPY_E_REDIS_TIMEOUT", "Redis connection pool pending queue is full.");
        }
        this.metrics.poolWait += 1;
        return await new Promise((resolve, reject) => {
            const waiter = { resolve, reject, timer: undefined };
            waiter.timer = setTimeout(() => {
                this.queue = this.queue.filter((entry) => entry !== waiter);
                this.metrics.poolRejected += 1;
                reject(redisError("SLOPPY_E_REDIS_TIMEOUT", "Redis connection acquisition timed out."));
            }, this.options.pool.pendingQueueTimeoutMs);
            this.queue.push(waiter);
        });
    }

    release(connection, reusable) {
        if (connection === undefined) {
            return;
        }
        reusable = reusable && !this.closed && !connection.closed && !connection.broken;
        if (this.queue.length > 0 && reusable) {
            const waiter = this.queue.shift();
            clearTimeout(waiter.timer);
            waiter.resolve(connection);
            return;
        }
        if (reusable) {
            this.idle.push({ connection, releasedAt: Date.now() });
            return;
        }
        this.active -= 1;
        this.metrics.connectionsClosed += 1;
        void connection.close();
        this._drainQueue();
    }

    _drainQueue() {
        while (!this.closed && this.queue.length > 0 && this.active < this.options.pool.maxConnections) {
            const waiter = this.queue.shift();
            clearTimeout(waiter.timer);
            this._create().then(waiter.resolve, waiter.reject);
        }
    }

    async close() {
        if (this.closed) {
            return;
        }
        this.closed = true;
        for (const waiter of this.queue.splice(0)) {
            clearTimeout(waiter.timer);
            waiter.reject(redisError("SLOPPY_E_REDIS_CLOSED", "Redis client is closed."));
        }
        const idle = this.idle.splice(0);
        await Promise.all(idle.map((entry) => entry.connection.close()));
        this.active = Math.max(0, this.active - idle.length);
    }

    stats() {
        return Object.freeze({
            activeConnections: this.active,
            idleConnections: this.idle.length,
            queuedRequests: this.queue.length,
        });
    }
}

function valueBytes(kind, bytes, maxValueBytes) {
    if (bytes.byteLength > maxValueBytes) {
        throw redisError("SLOPPY_E_REDIS_VALUE_TOO_LARGE", "Redis value exceeds maxValueBytes.");
    }
    const prefix = Text.utf8.encode(`${kind}:`);
    const output = new Uint8Array(prefix.byteLength + bytes.byteLength);
    output.set(prefix, 0);
    output.set(bytes, prefix.byteLength);
    return output;
}

function encodeJsonValue(value, maxValueBytes) {
    let text;
    try {
        text = JSON.stringify(value);
    } catch (error) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis JSON value could not be serialized.", { cause: error });
    }
    if (text === undefined) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis JSON value must be JSON serializable.");
    }
    return valueBytes("J", Text.utf8.encode(text), maxValueBytes);
}

function decodeValue(bytes, schemaOrOptions = undefined) {
    if (bytes === null) {
        return undefined;
    }
    if (!(bytes instanceof Uint8Array) || bytes.byteLength < 2 || bytes[1] !== 58) {
        return bytes;
    }
    const payload = bytes.subarray(2);
    const kind = String.fromCharCode(bytes[0]);
    if (kind === "T") {
        return Text.utf8.decode(payload);
    }
    if (kind === "B") {
        return Base64.decode(Text.utf8.decode(payload));
    }
    if (kind !== "J") {
        return bytes;
    }
    let value;
    try {
        value = JSON.parse(Text.utf8.decode(payload));
    } catch (error) {
        throw redisError("SLOPPY_E_REDIS_RESPONSE_VALIDATION_FAILED", "Redis JSON value could not be parsed.", { cause: error });
    }
    const schemaValue = Schema.isSchema(schemaOrOptions) ? schemaOrOptions : schemaOrOptions?.schema;
    if (schemaValue !== undefined) {
        try {
            return Schema.validate(value, schemaValue);
        } catch (error) {
            throw redisError("SLOPPY_E_REDIS_RESPONSE_VALIDATION_FAILED", "Redis JSON value failed schema validation.", { cause: error });
        }
    }
    return value;
}

function normalizeKey(key) {
    if (typeof key !== "string" || key.length === 0 || key.length > 1024 || key.includes("\0")) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis key must be a non-empty string up to 1024 bytes without NUL.");
    }
    return key;
}

function pxFromTtl(ttlMs, subject = "ttlMs") {
    if (ttlMs === undefined) {
        return undefined;
    }
    return normalizeTimeout(ttlMs, undefined, subject);
}

function setArgs(key, value, options = {}) {
    if (!isPlainObject(options)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis set options must be a plain object.");
    }
    const args = ["SET", normalizeKey(key), value];
    const ttlMs = pxFromTtl(options.ttlMs);
    if (ttlMs !== undefined) {
        args.push("PX", ttlMs);
    }
    if (options.nx === true) {
        args.push("NX");
    }
    if (options.xx === true) {
        args.push("XX");
    }
    if (options.get === true) {
        args.push("GET");
    }
    return args;
}

function createMetrics(name) {
    const registry = Metrics.defaultRegistry;
    return {
        name,
        commands: 0,
        commandErrors: 0,
        bytesIn: 0,
        bytesOut: 0,
        connectionsCreated: 0,
        connectionsReused: 0,
        connectionsClosed: 0,
        poolWait: 0,
        poolRejected: 0,
        lockAcquire: 0,
        lockTimeout: 0,
        recordCommand(command, outcome, durationMs) {
            this.commands += 1;
            if (outcome !== "ok") {
                this.commandErrors += 1;
            }
            registry.counter("redis.commands.total").inc({ client: name, command, outcome });
            registry.histogram("redis.command.duration").observe({ client: name, command, outcome }, durationMs);
        },
        snapshot(poolStats) {
            return Object.freeze({
                commandsTotal: this.commands,
                commandErrorsTotal: this.commandErrors,
                bytesInTotal: this.bytesIn,
                bytesOutTotal: this.bytesOut,
                connectionsCreated: this.connectionsCreated,
                connectionsReused: this.connectionsReused,
                connectionsClosed: this.connectionsClosed,
                poolWaitTotal: this.poolWait,
                poolRejectedTotal: this.poolRejected,
                locksAcquireTotal: this.lockAcquire,
                locksTimeoutTotal: this.lockTimeout,
                ...poolStats,
            });
        },
    };
}

function token(name) {
    assertName(name, "token name");
    return Object.freeze({
        __sloppyRedisToken: `redis.${name}`,
        toString() {
            return `redis.${name}`;
        },
    });
}

function tokenDisplay(tokenValue) {
    return tokenValue?.__sloppyRedisToken ?? String(tokenValue);
}

function randomOwnerToken() {
    try {
        return Random.hex(16);
    } catch {
        const bytes = new Uint8Array(16);
        if (globalThis.crypto?.getRandomValues !== undefined) {
            globalThis.crypto.getRandomValues(bytes);
        } else {
            for (let index = 0; index < bytes.length; index += 1) {
                bytes[index] = Math.floor(Math.random() * 256);
            }
        }
        return Array.from(bytes, (byte) => byte.toString(16).padStart(2, "0")).join("");
    }
}

function createRedisClient(name, rawOptions) {
    const options = normalizeClientOptions(name, rawOptions);
    const metrics = createMetrics(name);
    const pool = new RedisConnectionPool(options, metrics);
    let closed = false;
    const scripts = new Map();

    async function lease(callback) {
        if (closed) {
            throw redisError("SLOPPY_E_REDIS_CLOSED", "Redis client is closed.");
        }
        const connection = await pool.acquire();
        let reusable = true;
        try {
            return await callback(connection);
        } catch (error) {
            reusable = false;
            throw error;
        } finally {
            pool.release(connection, reusable);
        }
    }

    async function runCommand(commandName, args, commandOptions = undefined) {
        commandName = safeCommandName(commandName);
        const started = Date.now();
        try {
            const reply = await lease((connection) => connection.command([commandName, ...args], {
                timeoutMs: commandOptions?.timeoutMs,
                commandName,
            }));
            metrics.recordCommand(commandName, "ok", Date.now() - started);
            return reply;
        } catch (error) {
            metrics.recordCommand(commandName, "error", Date.now() - started);
            throw error;
        }
    }

    function closeClient() {
        if (closed) {
            return Promise.resolve();
        }
        closed = true;
        return pool.close();
    }

    const client = {
        name,
        token: token(name),
        async ping(message = undefined) {
            const reply = await runCommand("PING", message === undefined ? [] : [String(message)]);
            return replyText(reply);
        },
        async get(key, schemaOrOptions = undefined) {
            const reply = await runCommand("GET", [normalizeKey(key)]);
            return decodeValue(reply, schemaOrOptions);
        },
        async getText(key) {
            const reply = await runCommand("GET", [normalizeKey(key)]);
            if (reply === null) {
                return undefined;
            }
            const value = decodeValue(reply);
            return typeof value === "string" ? value : Text.utf8.decode(reply);
        },
        async getBytes(key) {
            const reply = await runCommand("GET", [normalizeKey(key)]);
            if (reply === null) {
                return undefined;
            }
            const value = decodeValue(reply);
            return value instanceof Uint8Array ? value : reply;
        },
        async set(key, value, setOptions = {}) {
            const reply = await runCommand("SET", setArgs(key, encodeJsonValue(value, options.maxValueBytes), setOptions).slice(1));
            return setOptions.get === true ? decodeValue(reply) : replyText(reply) === "OK";
        },
        async setText(key, value, setOptions = {}) {
            if (typeof value !== "string") {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis setText value must be a string.");
            }
            const reply = await runCommand("SET", setArgs(key, valueBytes("T", Text.utf8.encode(value), options.maxValueBytes), setOptions).slice(1));
            return setOptions.get === true ? decodeValue(reply) : replyText(reply) === "OK";
        },
        async setBytes(key, value, setOptions = {}) {
            if (!(value instanceof Uint8Array)) {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis setBytes value must be bytes.");
            }
            const encoded = Text.utf8.encode(Base64.encode(value));
            const reply = await runCommand("SET", setArgs(key, valueBytes("B", encoded, options.maxValueBytes), setOptions).slice(1));
            return setOptions.get === true ? decodeValue(reply) : replyText(reply) === "OK";
        },
        async delete(key) {
            return await runCommand("DEL", [normalizeKey(key)]);
        },
        async exists(key) {
            return await runCommand("EXISTS", [normalizeKey(key)]) === 1;
        },
        async mget(keys, schemaOrOptions = undefined) {
            if (!Array.isArray(keys) || keys.length === 0) {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis mget keys must be a non-empty array.");
            }
            const replies = await runCommand("MGET", keys.map(normalizeKey));
            if (!Array.isArray(replies)) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis MGET response must be an array.");
            }
            return replies.map((reply) => decodeValue(reply, schemaOrOptions));
        },
        async mset(entries, setOptions = {}) {
            const args = [];
            const iterable = entries instanceof Map ? entries.entries() : Object.entries(entries ?? {});
            for (const [key, value] of iterable) {
                args.push(normalizeKey(key), encodeJsonValue(value, options.maxValueBytes));
            }
            if (args.length === 0) {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis mset entries must not be empty.");
            }
            if (setOptions.ttlMs !== undefined) {
                await client.pipeline(args.reduce((commands, _value, index) => {
                    if (index % 2 === 0) {
                        commands.push(setArgs(args[index], args[index + 1], { ttlMs: setOptions.ttlMs }));
                    }
                    return commands;
                }, []));
                return true;
            }
            return replyText(await runCommand("MSET", args)) === "OK";
        },
        async incr(key, incrOptions = {}) {
            const amount = incrOptions.by ?? 1;
            return await runCommand(amount === 1 ? "INCR" : "INCRBY", amount === 1 ? [normalizeKey(key)] : [normalizeKey(key), amount]);
        },
        async decr(key, decrOptions = {}) {
            const amount = decrOptions.by ?? 1;
            return await runCommand(amount === 1 ? "DECR" : "DECRBY", amount === 1 ? [normalizeKey(key)] : [normalizeKey(key), amount]);
        },
        async expire(key, ttlMs) {
            return await runCommand("PEXPIRE", [normalizeKey(key), pxFromTtl(ttlMs)]) === 1;
        },
        async ttl(key) {
            return await runCommand("TTL", [normalizeKey(key)]);
        },
        async pttl(key) {
            return await runCommand("PTTL", [normalizeKey(key)]);
        },
        async scan(scanOptions = {}) {
            const cursor = scanOptions.cursor ?? "0";
            const args = [String(cursor)];
            if (scanOptions.match !== undefined) {
                args.push("MATCH", String(scanOptions.match));
            }
            if (scanOptions.count !== undefined) {
                args.push("COUNT", normalizeInteger(scanOptions.count, undefined, "scan.count", 1, 100000));
            }
            const reply = await runCommand("SCAN", args);
            if (!Array.isArray(reply) || reply.length !== 2 || !Array.isArray(reply[1])) {
                throw redisError("SLOPPY_E_REDIS_PROTOCOL_ERROR", "Redis SCAN response is malformed.");
            }
            return Object.freeze({
                cursor: replyText(reply[0]),
                keys: Object.freeze(reply[1].map(replyText)),
            });
        },
        async command(commandName, args = [], commandOptions = undefined) {
            if (!Array.isArray(args)) {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis command args must be an array.");
            }
            return await runCommand(commandName, args, commandOptions);
        },
        async pipeline(commands, pipelineOptions = undefined) {
            if (!Array.isArray(commands) || commands.length === 0) {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis pipeline commands must be a non-empty array.");
            }
            const normalized = commands.map((command) => {
                if (!Array.isArray(command) || command.length === 0) {
                    throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis pipeline command must be a non-empty array.");
                }
                return [safeCommandName(String(command[0])), ...command.slice(1)];
            });
            const started = Date.now();
            try {
                const replies = await lease((connection) => connection.pipeline(normalized, pipelineOptions));
                for (const command of normalized) {
                    metrics.recordCommand(command[0], "ok", Date.now() - started);
                }
                return replies;
            } catch (error) {
                for (const command of normalized) {
                    metrics.recordCommand(command[0], "error", Date.now() - started);
                }
                throw error;
            }
        },
        async withConnection(callback) {
            if (typeof callback !== "function") {
                throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis withConnection callback must be a function.");
            }
            return await lease((connection) => callback(Object.freeze({
                command(commandName, args = [], commandOptions = undefined) {
                    return connection.command([safeCommandName(commandName), ...args], {
                        ...commandOptions,
                        commandName: safeCommandName(commandName),
                    });
                },
            })));
        },
        async eval(script, keys = [], args = [], commandOptions = undefined) {
            validateScript(script);
            return await runCommand("EVAL", [script, keys.length, ...keys.map(normalizeKey), ...args], commandOptions);
        },
        async evalSha(sha, keys = [], args = [], commandOptions = undefined) {
            validateSha(sha);
            return await runCommand("EVALSHA", [sha, keys.length, ...keys.map(normalizeKey), ...args], commandOptions);
        },
        async scriptLoad(script) {
            validateScript(script);
            const sha = replyText(await runCommand("SCRIPT", ["LOAD", script]));
            scripts.set(script, sha);
            return sha;
        },
        async script(script, keys = [], args = [], commandOptions = undefined) {
            let sha = scripts.get(script);
            if (sha === undefined) {
                sha = await client.scriptLoad(script);
            }
            try {
                return await client.evalSha(sha, keys, args, commandOptions);
            } catch (error) {
                if (String(error?.message ?? error).includes("NOSCRIPT")) {
                    sha = await client.scriptLoad(script);
                    return await client.evalSha(sha, keys, args, commandOptions);
                }
                throw error;
            }
        },
        metrics() {
            return metrics.snapshot(pool.stats());
        },
        diagnostics() {
            return Object.freeze({
                name,
                url: options.endpoint.redactedUrl,
                host: options.endpoint.host,
                port: options.endpoint.port,
                database: options.endpoint.database,
                tls: options.endpoint.tls,
                closed,
                pool: pool.stats(),
            });
        },
        async health() {
            if (closed) {
                return { status: "unhealthy", errorCode: "SLOPPY_E_REDIS_CLOSED", data: { name } };
            }
            try {
                await client.ping();
                return { status: "healthy", data: { name } };
            } catch (error) {
                return {
                    status: "unhealthy",
                    errorCode: error?.code ?? "SLOPPY_E_REDIS_COMMAND_FAILED",
                    message: String(error?.message ?? error).replace(options.endpoint.password ?? "", SECRET_REDACTION),
                    data: { name, url: options.endpoint.redactedUrl },
                };
            }
        },
        close: closeClient,
        dispose: closeClient,
    };
    if (ASYNC_DISPOSE !== undefined) {
        client[ASYNC_DISPOSE] = closeClient;
    }
    Object.defineProperty(client, "__sloppyRedisRegistration", {
        value: Object.freeze({
            kind: "client",
            name,
            token: token(name),
            create: () => client,
            options: {
                url: options.endpoint.redactedUrl,
                database: options.endpoint.database,
                connectTimeoutMs: options.connectTimeoutMs,
                commandTimeoutMs: options.commandTimeoutMs,
                pool: options.pool,
            },
        }),
        enumerable: false,
    });
    return Object.freeze(client);
}

function validateScript(script) {
    if (typeof script !== "string" || script.length === 0 || script.length > 256 * 1024) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis Lua script must be a non-empty string up to 256 KiB.");
    }
}

function validateSha(sha) {
    if (typeof sha !== "string" || !/^[a-f0-9]{40}$/iu.test(sha)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis script SHA must be a 40-character hex string.");
    }
}

function normalizeLockOptions(options = {}) {
    if (!isPlainObject(options)) {
        throw redisError("SLOPPY_E_REDIS_INVALID_OPTIONS", "Redis lock options must be a plain object.");
    }
    return Object.freeze({
        prefix: options.prefix ?? "sloppy:locks:",
    });
}

function lockKey(prefix, name) {
    normalizeKey(name);
    return `${prefix}${name}`;
}

function createLocks(client, rawOptions = {}) {
    const options = normalizeLockOptions(rawOptions);
    async function acquire(name, acquireOptions = {}) {
        const ttlMs = pxFromTtl(acquireOptions.ttlMs ?? 30000);
        const waitTimeoutMs = normalizeNonNegativeTimeout(acquireOptions.waitTimeoutMs, 0, "lock.waitTimeoutMs");
        const retryDelayMs = normalizeTimeout(acquireOptions.retryDelayMs ?? 50, 50, "lock.retryDelayMs");
        const key = lockKey(options.prefix, name);
        const owner = randomOwnerToken();
        const started = Date.now();
        while (true) {
            const ok = replyText(await client.command("SET", [key, owner, "PX", ttlMs, "NX"])) === "OK";
            if (ok) {
                let released = false;
                const lease = {
                    name,
                    key,
                    owner: "[redacted]",
                    async extend(nextTtlMs) {
                        const result = await client.script(LOCK_EXTEND_SCRIPT, [key], [owner, pxFromTtl(nextTtlMs)]);
                        if (result !== 1) {
                            throw redisError("SLOPPY_E_REDIS_LOCK_LOST", "Redis lock lease is no longer owned by this client.");
                        }
                        return true;
                    },
                    async release() {
                        if (released) {
                            return false;
                        }
                        const result = await client.script(LOCK_RELEASE_SCRIPT, [key], [owner]);
                        released = true;
                        if (result !== 1) {
                            throw redisError("SLOPPY_E_REDIS_LOCK_RELEASE_FAILED", "Redis lock release did not own the lease.");
                        }
                        return true;
                    },
                    async dispose() {
                        if (!released) {
                            await lease.release().catch(() => {});
                        }
                    },
                };
                if (ASYNC_DISPOSE !== undefined) {
                    lease[ASYNC_DISPOSE] = lease.dispose;
                }
                return Object.freeze(lease);
            }
            if (waitTimeoutMs === 0 || Date.now() - started >= waitTimeoutMs) {
                throw redisError("SLOPPY_E_REDIS_LOCK_TIMEOUT", "Redis lock acquisition timed out.");
            }
            await new Promise((resolve) => setTimeout(resolve, Math.min(retryDelayMs, waitTimeoutMs - (Date.now() - started))));
        }
    }
    return Object.freeze({ acquire });
}

const Redis = Object.freeze({
    client: createRedisClient,
    locks: createLocks,
    token,
    encodeCommand,
    RespParser,
    SloppyRedisError,
    Secret,
    _redactUrl: redactRedisUrl,
    _tokenDisplay: tokenDisplay,
});

export {
    Redis,
    RedisErrorReply,
    RespParser,
    SloppyRedisError,
    decodeValue as __decodeRedisValue,
    encodeCommand,
    normalizeKey as __normalizeRedisKey,
    token as redisToken,
};
