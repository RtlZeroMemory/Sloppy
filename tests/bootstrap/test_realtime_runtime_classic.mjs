import assert from "node:assert/strict";
import { assertMemoryBackplaneConformance } from "./realtime_backplane_conformance.mjs";

const previousRuntime = globalThis.__sloppy_runtime;
await import("../../stdlib/sloppy/internal/runtime-classic.js");

try {
    const { Realtime, SloppyRealtimeError, Schema } = globalThis.__sloppy_runtime;

    assert.equal(typeof Realtime.channel, "function");
    assert.equal(typeof Realtime.event, "function");
    assert.equal(typeof Realtime.backplane.memory, "function");
    assert.equal(typeof Realtime.__route, "function");
    assert.equal(SloppyRealtimeError.name, "SloppyRealtimeError");

    const Chat = Realtime.channel("chat", {
        client: {
            sendMessage: Schema.object({ text: Schema.string() }),
            typing: Realtime.event(Schema.object({ roomId: Schema.string() }))
                .requiresAuth()
                .requiresScope("chat:write", "rooms:write")
                .requiresRole("moderator")
                .authorize("Chat.Send"),
        },
        server: {
            messageCreated: Schema.object({ text: Schema.string() }),
        },
    });

    assert.equal(Realtime.isChannel(Chat), true);
    assert.equal(Chat.metadata.protocol, "sloppy.realtime.chat.v1");
    assert.equal(Chat.metadata.client.typing.auth.required, true);
    assert.deepEqual(Chat.metadata.client.typing.auth.scopes, ["chat:write", "rooms:write"]);
    assert.deepEqual(Chat.metadata.client.typing.auth.roles, ["moderator"]);
    assert.equal(Chat.metadata.client.typing.auth.policy, "Chat.Send");
    assert.equal(typeof Realtime.event(Schema.string()).requiresUser, "undefined");
    assert.deepEqual(
        Realtime.event(Schema.string(), { scopes: "chat:write", roles: "moderator" }).metadata.auth,
        {
            required: false,
            scopes: ["chat:write"],
            roles: ["moderator"],
            policy: undefined,
        },
    );

    assert.deepEqual(
        Chat.serializeClientMessage("sendMessage", { text: "hello" }),
        { type: "sendMessage", data: { text: "hello" } },
    );
    assert.deepEqual(
        Chat.parseServerMessage({ type: "messageCreated", data: { text: "hi" } }),
        { type: "messageCreated", data: { text: "hi" } },
    );
    assert.throws(
        () => Chat.serializeClientMessage("sendMessage", { text: 1 }),
        (error) => error instanceof SloppyRealtimeError &&
            error.code === "SLOPPY_E_REALTIME_VALIDATION_FAILED",
    );

    const Primitive = Realtime.channel("primitive", {
        client: {
            text: Schema.string(),
            list: Schema.array(Schema.string()),
            nothing: Schema.literal(null),
            object: Schema.object({ ok: Schema.boolean() }),
        },
        server: {
            reply: Schema.string(),
        },
    });
    assert.deepEqual(Primitive.parseClientMessage({ type: "text", data: "hello" }), { type: "text", data: "hello" });
    assert.deepEqual(Primitive.parseClientMessage({ type: "list", data: ["a"] }), { type: "list", data: ["a"] });
    assert.deepEqual(Primitive.parseClientMessage({ type: "nothing", data: null }), { type: "nothing", data: null });
    assert.deepEqual(Primitive.parseClientMessage({ type: "object", data: { ok: true } }), { type: "object", data: { ok: true } });
    assert.throws(
        () => Primitive.parseClientMessage({ type: "text" }),
        (error) => error instanceof SloppyRealtimeError &&
            error.code === "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
    );
    assert.throws(
        () => Primitive.parseClientMessage("{"),
        (error) => error instanceof SloppyRealtimeError &&
            error.code === "SLOPPY_E_REALTIME_MALFORMED_JSON",
    );

    const Namespaced = Realtime.channel("chat:rooms", {
        client: { ask: Schema.string() },
        server: { answer: Schema.string() },
    });
    assert.equal(Namespaced.metadata.protocol, "sloppy.realtime.chat-rooms.v1");

    const backplane = Realtime.backplane.memory();
    const delivered = [];
    await backplane.connect({
        connectionId: "c1",
        async send(envelope) {
            delivered.push(envelope);
        },
    });
    await backplane.join("c1", "room:one");
    await backplane.broadcast("room:one", { type: "messageCreated", data: { text: "ok" } });
    assert.deepEqual(delivered, [{ type: "messageCreated", data: { text: "ok" } }]);
    assert.deepEqual(await backplane.groups("c1"), ["room:one"]);
    await backplane.disconnect("c1");
    assert.deepEqual(await backplane.groups("c1"), []);
    await assertMemoryBackplaneConformance(() => Realtime.backplane.memory());

    const sent = [];
    const route = Realtime.__route(Chat, async (ctx) => {
        await ctx.accept();
        ctx.on("sendMessage", async (input) => {
            await ctx.send("messageCreated", { text: input.text });
        });
    }, { protocols: ["sloppy.realtime.chat.v1"] });
    assert.deepEqual(route[Symbol.for("sloppy.websocket.routeOptions")].protocols, ["sloppy.realtime.chat.v1"]);
    await route({
        __sloppyWebSocketHandshake: true,
        __sloppyWebSocket: {
            id: "socket-1",
            closed: false,
            __setContext() {},
            async accept() {},
            async sendJson(envelope) {
                sent.push(envelope);
            },
            async close() {
                this.closed = true;
            },
            async *messages() {
                yield {
                    kind: "json",
                    json() {
                        return { type: "sendMessage", data: { text: "from-runtime" } };
                    },
                };
            },
        },
        routePattern: "/chat",
        request: { headers: new Map(), query: new Map() },
        requireUser() {
            return this.user;
        },
    });
    assert.deepEqual(sent, [{ type: "messageCreated", data: { text: "from-runtime" } }]);

    const policySent = [];
    const Policy = Realtime.channel("policy", {
        client: {
            send: Realtime.event(Schema.object({ text: Schema.string() })).authorize("Realtime.Send"),
        },
        server: {
            accepted: Schema.object({ text: Schema.string() }),
        },
    });
    const policyRoute = Realtime.__route(Policy, async (ctx) => {
        await ctx.accept();
        ctx.on("send", async (input) => {
            await ctx.send("accepted", { text: input.text });
        });
    });
    await policyRoute({
        __sloppyWebSocketHandshake: true,
        __sloppyWebSocket: {
            id: "policy-deny",
            closed: false,
            __setContext() {},
            async accept() {},
            async sendJson(envelope) {
                policySent.push(envelope);
            },
            async close() {
                this.closed = true;
            },
            async *messages() {
                yield {
                    kind: "json",
                    json() {
                        return { type: "send", data: { text: "deny" } };
                    },
                };
            },
        },
        user: { authenticated: true, scopes: [], roles: [] },
        async authorize() {
            return false;
        },
        routePattern: "/policy",
        request: { headers: new Map(), query: new Map() },
        requireUser() {
            return this.user;
        },
    });
    assert.deepEqual(policySent, [{
        type: "error",
        error: {
            code: "SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT",
            message: "Realtime event authorization policy denied the message.",
            event: "send",
        },
    }]);

    policySent.length = 0;
    await policyRoute({
        __sloppyWebSocketHandshake: true,
        __sloppyWebSocket: {
            id: "policy-allow",
            closed: false,
            __setContext() {},
            async accept() {},
            async sendJson(envelope) {
                policySent.push(envelope);
            },
            async close() {
                this.closed = true;
            },
            async *messages() {
                yield {
                    kind: "json",
                    json() {
                        return { type: "send", data: { text: "allow" } };
                    },
                };
            },
        },
        user: { authenticated: true, scopes: [], roles: [] },
        async authorize(_policy, resource) {
            return resource?.data?.text === "allow";
        },
        routePattern: "/policy",
        request: { headers: new Map(), query: new Map() },
        requireUser() {
            return this.user;
        },
    });
    assert.deepEqual(policySent, [{ type: "accepted", data: { text: "allow" } }]);

    const initFailureSent = [];
    const InitFailure = Realtime.channel("initFailure", {
        client: { send: Schema.object({ text: Schema.string() }) },
        server: { accepted: Schema.object({ text: Schema.string() }) },
    });
    const initFailureRoute = Realtime.__route(InitFailure, async (ctx) => {
        await ctx.accept();
        throw new Error("internal detail");
    }, { handlerErrorPolicy: "error" });
    await initFailureRoute({
        __sloppyWebSocketHandshake: true,
        __sloppyWebSocket: {
            id: "init-failure",
            closed: false,
            __setContext() {},
            async accept() {},
            async sendJson(envelope) {
                initFailureSent.push(envelope);
            },
            async close() {
                this.closed = true;
            },
            async *messages() {},
        },
        routePattern: "/init-failure",
        request: { headers: new Map(), query: new Map() },
        requireUser() {
            return this.user;
        },
    });
    assert.deepEqual(initFailureSent, [{
        type: "error",
        error: {
            code: "SLOPPY_E_REALTIME_HANDLER_ERROR",
            message: "Realtime message handling failed.",
        },
    }]);
} finally {
    if (previousRuntime === undefined) {
        delete globalThis.__sloppy_runtime;
    } else {
        globalThis.__sloppy_runtime = previousRuntime;
    }
}
