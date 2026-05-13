import assert from "node:assert/strict";

import { Base64, Redis, Schema, SloppyRedisError, Text } from "../../stdlib/sloppy/index.js";
import { RedisErrorReply, RespParser, encodeCommand } from "../../stdlib/sloppy/redis.js";

function decodeCommand(bytes) {
    const text = Text.utf8.decode(bytes);
    const lines = text.split("\r\n");
    assert.match(lines[0], /^\*[0-9]+$/u);
    const args = [];
    for (let index = 1; index < lines.length - 1;) {
        assert.match(lines[index], /^\$[0-9]+$/u);
        args.push(lines[index + 1]);
        index += 2;
    }
    return args;
}

function bulk(value) {
    if (value === null) {
        return "$-1\r\n";
    }
    const text = value instanceof Uint8Array ? Text.utf8.decode(value) : String(value);
    return `$${Text.utf8.encode(text).byteLength}\r\n${text}\r\n`;
}

function array(values) {
    return `*${values.length}\r\n${values.join("")}`;
}

function fakeBridge(handler) {
    let nextHandle = 1;
    const handles = new Map();
    return {
        connect(options) {
            handles.set(nextHandle, { options, reads: [], writes: [], closed: false });
            return Promise.resolve(nextHandle++);
        },
        async write(handle, bytes) {
            const state = handles.get(handle);
            state.writes.push(decodeCommand(bytes));
            const response = await handler(state.writes.at(-1), state);
            if (Array.isArray(response)) {
                state.reads.push(...response.map((entry) => Text.utf8.encode(entry)));
            } else {
                state.reads.push(Text.utf8.encode(response));
            }
        },
        async read(handle) {
            const state = handles.get(handle);
            return state.reads.shift() ?? new Uint8Array(0);
        },
        async close(handle) {
            handles.get(handle).closed = true;
        },
        async abort(handle) {
            handles.get(handle).closed = true;
        },
        handles,
    };
}

function installBridge(bridge) {
    const previous = globalThis.__sloppy;
    globalThis.__sloppy = { ...(previous ?? {}), net: bridge };
    return () => {
        globalThis.__sloppy = previous;
    };
}

assert.equal(
    Text.utf8.decode(encodeCommand(["GET", "key"])),
    "*2\r\n$3\r\nGET\r\n$3\r\nkey\r\n",
);

for (const [wire, expected] of [
    ["+OK\r\n", "OK"],
    [":1\r\n", 1],
    ["$5\r\nhello\r\n", Text.utf8.encode("hello")],
    ["$-1\r\n", null],
]) {
    const parser = new RespParser();
    parser.feed(Text.utf8.encode(wire));
    const actual = parser.read();
    if (actual instanceof Uint8Array) {
        assert.deepEqual(actual, expected);
    } else {
        assert.deepEqual(actual, expected);
    }
}

const nested = new RespParser();
nested.feed(Text.utf8.encode("*2\r\n+OK\r\n*2\r\n:1\r\n$3\r\ntwo\r\n"));
const nestedReply = nested.read();
assert.equal(nestedReply[0], "OK");
assert.equal(nestedReply[1][0], 1);
assert.equal(Text.utf8.decode(nestedReply[1][1]), "two");

const fragmented = new RespParser();
fragmented.feed(Text.utf8.encode("$5\r\nhe"));
assert.equal(fragmented.read(), undefined);
fragmented.feed(Text.utf8.encode("llo\r\n"));
assert.equal(Text.utf8.decode(fragmented.read()), "hello");

assert.throws(() => {
    const parser = new RespParser({ maxResponseBytes: 8 });
    parser.feed(Text.utf8.encode("$99\r\n"));
    parser.read();
}, /SLOPPY_E_REDIS_PROTOCOL_ERROR/u);

const errorParser = new RespParser();
errorParser.feed(Text.utf8.encode("-ERR wrongtype\r\n"));
const errorReply = errorParser.read();
assert.equal(errorReply instanceof RedisErrorReply, true);
assert.equal(errorReply.code, "ERR");

{
    const bridge = fakeBridge((command) => {
        switch (command[0]) {
            case "AUTH":
                assert.deepEqual(command, ["AUTH", "default", "secret"]);
                return "+OK\r\n";
            case "SELECT":
                assert.deepEqual(command, ["SELECT", "2"]);
                return "+OK\r\n";
            case "PING":
                return "+PONG\r\n";
            case "SET":
                return "+OK\r\n";
            case "GET":
                return bulk("J:{\"ok\":true}");
            case "MGET":
                return array([bulk("J:1"), "$-1\r\n"]);
            case "SCRIPT":
                return bulk("0123456789abcdef0123456789abcdef01234567");
            case "EVALSHA":
                return ":1\r\n";
            default:
                return "+OK\r\n";
        }
    });
    const restore = installBridge(bridge);
    try {
        const client = Redis.client("main", {
            url: "redis://default:secret@localhost:6379/2",
            connectTimeoutMs: 100,
            commandTimeoutMs: 100,
            pool: { maxConnections: 1, pendingQueueLimit: 2, pendingQueueTimeoutMs: 100 },
        });
        assert.equal(await client.ping(), "PONG");
        assert.equal(await client.set("hello", { ok: true }, { ttlMs: 1000 }), true);
        assert.deepEqual(await client.get("hello", Schema.object({ ok: Schema.boolean() })), { ok: true });
        assert.deepEqual(await client.mget(["one", "two"]), [1, undefined]);
        assert.equal(await client.script("return 1", [], []), 1);
        assert.equal(client.diagnostics().url.includes("secret"), false);
        assert.equal(client.metrics().connectionsCreated, 1);
        await client.dispose();
        await client.dispose();
        await assert.rejects(() => client.ping(), /SLOPPY_E_REDIS_CLOSED/u);
    } finally {
        restore();
    }
}

{
    assert.throws(() => Redis.client("bad", { url: "http://localhost" }), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
    assert.throws(() => Redis.client("bad name", { url: "redis://localhost" }), /SLOPPY_E_REDIS_INVALID_OPTIONS/u);
}

{
    const bridge = fakeBridge(() => "-ERR invalid password\r\n");
    const restore = installBridge(bridge);
    try {
        const client = Redis.client("authfail", {
            url: "redis://:secret@localhost/0",
            connectTimeoutMs: 100,
            commandTimeoutMs: 100,
            pool: { maxConnections: 1 },
        });
        await assert.rejects(() => client.ping(), /SLOPPY_E_REDIS_AUTH_FAILED/u);
    } finally {
        restore();
    }
}

{
    const client = Redis.client("redact", {
        url: "redis://:topsecret@localhost/0",
        password: "topsecret",
    });
    assert.equal(JSON.stringify(client.diagnostics()).includes("topsecret"), false);
    await client.dispose();
}

assert.equal(Base64.encode(Text.utf8.encode("redis")), "cmVkaXM=");
