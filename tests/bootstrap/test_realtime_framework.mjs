import assert from "node:assert/strict";

import {
    Auth,
    Health,
    Realtime,
    Sloppy,
    SloppyRealtimeError,
    TestHost,
    Text,
    schema,
} from "../../stdlib/sloppy/index.js";
import { assertMemoryBackplaneConformance } from "./realtime_backplane_conformance.mjs";

function assertThrowsMessage(fn, pattern) {
    assert.throws(fn, (error) => {
        assert.match(error.message, pattern);
        return true;
    });
}

function assertRealtimeError(fn, code) {
    assert.throws(fn, (error) => {
        assert.equal(error instanceof SloppyRealtimeError, true);
        assert.equal(error.code, code);
        return true;
    });
}

{
    const Message = schema.object({
        roomId: schema.string(),
        text: schema.string().maxLength(1000),
    });
    const Chat = Realtime.channel("chat", {
        client: {
            sendMessage: Message,
            typing: Realtime.event(schema.object({ roomId: schema.string() })).requiresScope("chat:write"),
        },
        server: {
            messageCreated: schema.object({
                id: schema.string(),
                roomId: schema.string(),
                text: schema.string(),
            }),
        },
    });

    assert.equal(Realtime.isChannel(Chat), true);
    assert.equal(Chat.name, "chat");
    assert.equal(Chat.metadata.protocol, "sloppy.realtime.chat.v1");
    assert.deepEqual(Object.keys(Chat.client), ["sendMessage", "typing"]);
    assert.deepEqual(Object.keys(Chat.server), ["messageCreated"]);
    assert.equal(Object.isFrozen(Chat), true);
    assert.equal(Object.isFrozen(Chat.client), true);
    assert.equal(Object.isFrozen(Chat.metadata.client.sendMessage.schema), true);
    assert.deepEqual(Chat.metadata.client.typing.auth.scopes, ["chat:write"]);

    const Secured = Realtime.event(schema.string())
        .requiresAuth()
        .requiresScope("messages:write", "rooms:write")
        .requiresRole("admin")
        .authorize("Messages.Write");
    assert.deepEqual(Secured.metadata.auth, {
        required: true,
        scopes: ["messages:write", "rooms:write"],
        roles: ["admin"],
        policy: "Messages.Write",
    });

    const input = Chat.validateClientEvent("sendMessage", {
        roomId: "r1",
        text: "hello",
    });
    assert.deepEqual(input, { roomId: "r1", text: "hello" });

    const parsed = Chat.parseClientMessage(JSON.stringify({
        type: "sendMessage",
        data: { roomId: "r1", text: "hello" },
        id: "client-1",
    }));
    assert.deepEqual(parsed, {
        type: "sendMessage",
        data: { roomId: "r1", text: "hello" },
        id: "client-1",
    });

    const server = Chat.serializeServerMessage("messageCreated", {
        id: "m1",
        roomId: "r1",
        text: "hello",
    });
    assert.deepEqual(server, {
        type: "messageCreated",
        data: { id: "m1", roomId: "r1", text: "hello" },
    });
    assert.equal(Chat.stringifyServerMessage("messageCreated", server.data), JSON.stringify(server));
}

{
    assertThrowsMessage(
        () => Realtime.channel("", { client: {}, server: {} }),
        /channel name/,
    );
    assertThrowsMessage(
        () => Realtime.channel("chat", { client: { "bad name": schema.string() }, server: {} }),
        /stable identifier/,
    );
    assertThrowsMessage(
        () => Realtime.channel("chat", { client: { ping: schema.string() }, server: {} }),
        /reserved/,
    );
    assertThrowsMessage(
        () => Realtime.channel("chat", { client: { same: schema.string() }, server: { same: schema.string() } }),
        /both a client and server event/,
    );
    assertThrowsMessage(
        () => Realtime.channel("chat", { client: { sendMessage: { validate() {} } }, server: {} }),
        /Sloppy schema/,
    );
    assertThrowsMessage(
        () => Realtime.event({ validate() {} }),
        /Sloppy schema/,
    );
}

{
    const Chat = Realtime.channel("chat", {
        client: {
            sendMessage: schema.object({ text: schema.string().maxLength(5) }),
        },
        server: {
            messageCreated: schema.object({ text: schema.string().maxLength(5) }),
        },
    });

    assertRealtimeError(
        () => Chat.parseClientMessage("{"),
        "SLOPPY_E_REALTIME_MALFORMED_JSON",
    );
    assertRealtimeError(
        () => Chat.parseClientMessage(JSON.stringify({ type: "sendMessage" })),
        "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
    );
    assertRealtimeError(
        () => Chat.parseClientMessage(JSON.stringify({ type: "missing", data: {} })),
        "SLOPPY_E_REALTIME_UNKNOWN_EVENT",
    );
    assertRealtimeError(
        () => Chat.parseClientMessage(JSON.stringify({ type: "sendMessage", data: { text: "too long" } })),
        "SLOPPY_E_REALTIME_VALIDATION_FAILED",
    );
    assertRealtimeError(
        () => Chat.serializeServerMessage("messageCreated", { text: "too long" }),
        "SLOPPY_E_REALTIME_VALIDATION_FAILED",
    );

    const error = new SloppyRealtimeError(
        "SLOPPY_E_REALTIME_VALIDATION_FAILED",
        "Realtime message validation failed.",
        {
            event: "sendMessage",
            issues: [{
                path: ["text"],
                code: "maxLength",
                message: "The value was too long.".repeat(20),
            }],
        },
    );
    const envelope = Chat.errorEnvelope(error);
    assert.equal(envelope.type, "error");
    assert.equal(envelope.error.code, "SLOPPY_E_REALTIME_VALIDATION_FAILED");
    assert.equal(envelope.error.event, "sendMessage");
    assert.equal(envelope.error.issues[0].message.length <= 163, true);
}

{
    const Primitive = Realtime.channel("primitive", {
        client: {
            text: schema.string(),
            list: schema.array(schema.string()),
            nothing: schema.literal(null),
            object: schema.object({ ok: schema.boolean() }),
        },
        server: {
            reply: schema.string(),
        },
    });
    assert.deepEqual(Primitive.parseClientMessage({ type: "text", data: "hello" }), { type: "text", data: "hello" });
    assert.deepEqual(Primitive.parseClientMessage({ type: "list", data: ["a", "b"] }), { type: "list", data: ["a", "b"] });
    assert.deepEqual(Primitive.parseClientMessage({ type: "nothing", data: null }), { type: "nothing", data: null });
    assert.deepEqual(Primitive.parseClientMessage({ type: "object", data: { ok: true } }), { type: "object", data: { ok: true } });
    assertRealtimeError(
        () => Primitive.parseClientMessage({ type: "text" }),
        "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE",
    );
}

{
    const Namespaced = Realtime.channel("chat:rooms", {
        client: { ask: schema.string() },
        server: { answer: schema.string() },
    });
    assert.equal(Namespaced.metadata.protocol, "sloppy.realtime.chat-rooms.v1");
}

{
    const backplane = Realtime.backplane.memory();
    const check = Health.realtime(backplane);
    assert.equal((await check()).status, "healthy");
    await backplane.dispose();
    assert.equal((await check()).status, "unhealthy");
    await assertMemoryBackplaneConformance(() => Realtime.backplane.memory());
}

{
    const Stress = Realtime.channel("stress", {
        client: { doPing: schema.object({ i: schema.int() }) },
        server: { didPong: schema.object({ i: schema.int() }) },
    });
    const malformedInputs = [
        ["", "SLOPPY_E_REALTIME_MALFORMED_JSON"],
        ["{", "SLOPPY_E_REALTIME_MALFORMED_JSON"],
        ["[]", "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE"],
        ["{}", "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE"],
        [JSON.stringify({ type: "missingData" }), "SLOPPY_E_REALTIME_MALFORMED_ENVELOPE"],
        [JSON.stringify({ type: "doPing", data: { i: "not-int" } }), "SLOPPY_E_REALTIME_VALIDATION_FAILED"],
        [JSON.stringify({ type: "unknown", data: {} }), "SLOPPY_E_REALTIME_UNKNOWN_EVENT"],
    ];
    for (const [input, code] of malformedInputs) {
        assert.throws(
            () => Stress.parseClientMessage(input),
            (error) => error instanceof SloppyRealtimeError && error.code === code,
        );
    }

    const backplane = Realtime.backplane.memory();
    const delivered = [];
    for (let i = 0; i < 64; i += 1) {
        await backplane.connect({
            connectionId: `c${i}`,
            async send(envelope) {
                delivered.push(envelope);
            },
        });
        await backplane.join(`c${i}`, "stress:all");
        await backplane.presenceSet(`c${i}`, { userId: `u${i}` });
    }
    assert.equal((await backplane.broadcast("stress:all", { type: "didPong", data: { i: 1 } })).count, 64);
    assert.equal(delivered.length, 64);
    assert.equal(Text.utf8.encode(JSON.stringify({ note: "é".repeat(2050) })).byteLength > 4096, true);
    assert.throws(
        () => backplane.presenceSet("c1", { metadata: { note: "é".repeat(2050) } }),
        /presence metadata must be bounded JSON/,
    );
    assert.equal((await backplane.presenceInGroup("stress:all")).length, 64);
    await backplane.disconnect("c0");
    assert.equal((await backplane.presenceInGroup("stress:all")).length, 63);
}

{
    const PolicyChannel = Realtime.channel("policy", {
        client: {
            send: Realtime.event(schema.object({ text: schema.string() })).authorize("Realtime.Send"),
        },
        server: {
            accepted: schema.object({ text: schema.string() }),
        },
    });
    const app = Sloppy.create();
    app.auth.addPolicy("Realtime.Send", Auth.policy((policy) =>
        policy.custom((_user, _ctx, resource) => resource?.data?.text === "allow")));
    let handled = 0;
    app.realtime("/policy", PolicyChannel, async (ctx) => {
        await ctx.accept();
        ctx.on("send", async (input) => {
            handled += 1;
            await ctx.send("accepted", { text: input.text });
        });
    }, { validationFailurePolicy: "error" });

    const host = await TestHost.create(app);
    try {
        const denied = await host.realtime("/policy", PolicyChannel)
            .asUser({ sub: "denied" })
            .connect();
        await denied.send("send", { text: "deny" });
        await denied.expectError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT");
        assert.equal(handled, 0);
        await denied.close();

        const allowed = await host.realtime("/policy", PolicyChannel)
            .asUser({ sub: "allowed" })
            .connect();
        await allowed.send("send", { text: "allow" });
        await allowed.expect("accepted", { text: "allow" });
        assert.equal(handled, 1);
        await allowed.close();
    } finally {
        await host.close();
    }
}

{
    const Chat = Realtime.channel("chat", {
        client: {
            askPing: schema.object({ text: schema.string() }),
        },
        server: {
            pongReady: schema.object({ text: schema.string() }),
        },
    });
    const app = Sloppy.create();
    app.realtime("/rooms/{roomId}", Chat, async (ctx) => {
        assert.equal(ctx.params.roomId, "r1");
        assert.equal(ctx.channel, Chat);
        assert.equal(ctx.user.sub, "alice");
        await ctx.accept();
        await ctx.socket.sendJson(Chat.serializeServerMessage("pongReady", { text: "ready" }));
        await ctx.close();
    }, {
        maxMessageBytes: 4096,
        presence: true,
        unknownEventPolicy: "close",
    })
        .withName("Chat.Room")
        .requiresAuth()
        .requiresScope("chat")
        .allowedOrigins(["https://app.example.com"]);

    const route = app.__getRoutes()[0];
    assert.equal(route.kind, "websocket");
    assert.equal(route.metadata.realtime.kind, "realtime");
    assert.equal(route.metadata.realtime.channel.name, "chat");
    assert.equal(route.metadata.realtime.options.presence, true);
    assert.equal(route.metadata.realtime.options.unknownEventPolicy, "close");
    assert.deepEqual(route.metadata.realtime.websocket.protocols, ["sloppy.realtime.chat.v1"]);
    assert.deepEqual(route.metadata.realtime.websocket.origins, ["https://app.example.com"]);
    assert.equal(route.metadata.realtime.websocket.maxMessageBytes, 4096);
    assert.deepEqual(route.metadata.auth.scopes, ["chat"]);

    const host = await TestHost.create(app);
    try {
        const client = await host.websocket("/rooms/r1")
            .origin("https://app.example.com")
            .protocols(["sloppy.realtime.chat.v1"])
            .asUser({ sub: "alice", scopes: ["chat"] })
            .connect();
        await client.expectJson({ type: "pongReady", data: { text: "ready" } });
        await client.expectClose(1000);
    } finally {
        await host.close();
    }

    const grouped = Sloppy.create();
    grouped.group("/api")
        .requiresAuth()
        .realtime("/rooms/{roomId}", Chat, async (ctx) => {
            await ctx.accept();
        });
    const groupedRoute = grouped.__getRoutes()[0];
    assert.equal(groupedRoute.pattern, "/api/rooms/{roomId}");
    assert.equal(groupedRoute.metadata.realtime.kind, "realtime");
    assert.equal(groupedRoute.metadata.auth.required, true);

    assertThrowsMessage(
        () => Sloppy.create().realtime("/bad", {}, async () => {}),
        /Realtime.channel/,
    );
    assertThrowsMessage(
        () => Sloppy.create().realtime("/bad", Chat, undefined),
        /handler/,
    );
    assertThrowsMessage(
        () => Sloppy.create().realtime("/bad", Chat, async () => {}, { unknownEventPolicy: "drop" }),
        /unknownEventPolicy/,
    );
}

{
    const Chat = Realtime.channel("chat", {
        client: {
            sendMessage: schema.object({
                roomId: schema.string(),
                text: schema.string().maxLength(1000),
            }),
            typing: Realtime.event(schema.object({ roomId: schema.string() })).requiresScope("chat:write"),
            who: schema.object({ roomId: schema.string() }),
        },
        server: {
            ready: schema.object({ roomId: schema.string() }),
            messageCreated: schema.object({
                roomId: schema.string(),
                text: schema.string(),
            }),
            userTyping: schema.object({
                roomId: schema.string(),
                userId: schema.string(),
            }),
            presenceList: schema.object({
                count: schema.int(),
            }),
        },
    });
    const app = Sloppy.create();
    app.realtime("/rooms/{roomId}", Chat, async (ctx) => {
        await ctx.accept();
        await ctx.groups.join(`room:${ctx.params.roomId}`);
        await ctx.presence.set({ metadata: { status: "online" } });
        await ctx.send("ready", { roomId: ctx.params.roomId });
        ctx.on("sendMessage", async (input) => {
            await ctx.group(`room:${ctx.params.roomId}`).broadcast("messageCreated", {
                roomId: ctx.params.roomId,
                text: input.text,
            });
        });
        ctx.on("typing", async (input) => {
            await ctx.group(`room:${input.roomId}`).broadcast("userTyping", {
                roomId: input.roomId,
                userId: ctx.user.sub,
            }, { exceptSelf: true });
        });
        ctx.on("who", async (input) => {
            const users = await ctx.presence.inGroup(`room:${input.roomId}`);
            await ctx.send("presenceList", { count: users.length });
        });
    }, { presence: true, validationFailurePolicy: "error" }).requiresAuth();

    const host = await TestHost.create(app);
    try {
        const openapi = await host.openapi();
        assert.deepEqual(openapi.paths["/rooms/{roomId}"].get["x-slop-realtime"], {
            kind: "realtime",
            channel: "chat",
            transport: "websocket",
        });
        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat:write"] })
            .connect();
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: [] })
            .connect();
        await alice.expect("ready", { roomId: "r1" });
        await bob.expect("ready", { roomId: "r1" });

        await alice.send("sendMessage", { roomId: "r1", text: "hello" });
        await bob.expect("messageCreated", { roomId: "r1", text: "hello" });

        await alice.send("typing", { roomId: "r1" });
        await bob.expect("userTyping", { roomId: "r1", userId: "alice" });

        await bob.send("typing", { roomId: "r1" });
        await bob.expectError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT");

        assert.throws(
            () => bob.send("sendMessage", { roomId: "r1", text: "x".repeat(1001) }),
            /Realtime message validation failed/,
        );

        await bob.send("who", { roomId: "r1" });
        await bob.expect("presenceList", { count: 2 });
        host.metrics.expectGauge("realtime.connections.active", 2, {
            route: "/rooms/{roomId}",
            channel: "chat",
        });

        await alice.close();
        await bob.close();
    } finally {
        await host.close();
    }
    host.metrics.expectCounter("realtime.groups.join.total", 2, {
        route: "/rooms/{roomId}",
        channel: "chat",
    });
    host.metrics.expectCounter("realtime.presence.set.total", 2, {
        route: "/rooms/{roomId}",
        channel: "chat",
    });
    host.metrics.expectGauge("realtime.connections.active", 0, {
        route: "/rooms/{roomId}",
        channel: "chat",
    });
}
