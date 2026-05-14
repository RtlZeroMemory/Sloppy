import assert from "node:assert/strict";

import { Realtime, Sloppy, TestHost, schema } from "../../../stdlib/sloppy/index.js";
import { createFinding, createReport } from "../runner/contract-report.mjs";

const SUBSYSTEM = "realtime";

function passFinding(fixture, invariant, message, details = undefined) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        fixture,
        invariant,
        message,
        details,
    });
}

function failFinding(fixture, invariant, error) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.${invariant}`,
        status: "fail",
        severity: "error",
        subsystem: SUBSYSTEM,
        fixture,
        invariant,
        message: error instanceof Error ? error.message : String(error),
        details: error instanceof Error ? { name: error.name, stack: error.stack } : undefined,
    });
}

async function expectRejectsStatus(connectAttempt, status) {
    await connectAttempt.expectRejected(status);
}

async function assertSocketCannotReceive(client) {
    await assert.rejects(
        () => client.expect("messageCreated", undefined, { timeoutMs: 20 }),
        /closed|waiting|close/u,
    );
}

async function runCase(fixture, invariant, body, message, details = undefined) {
    try {
        await body();
        return passFinding(fixture, invariant, message, details);
    } catch (error) {
        return failFinding(fixture, invariant, error);
    }
}

function createChatChannel() {
    return Realtime.channel("contractChat", {
        client: {
            sendMessage: schema.object({
                roomId: schema.string(),
                text: schema.string().maxLength(32),
            }),
            leaveRoom: schema.object({ roomId: schema.string() }),
            who: schema.object({ roomId: schema.string() }),
            typing: Realtime.event(schema.object({ roomId: schema.string() })).requiresScope("chat:write"),
        },
        server: {
            ready: schema.object({ roomId: schema.string(), userId: schema.string() }),
            messageCreated: schema.object({ roomId: schema.string(), text: schema.string() }),
            presenceList: schema.object({ roomId: schema.string(), count: schema.int() }),
            userTyping: schema.object({ roomId: schema.string(), userId: schema.string() }),
        },
    });
}

function createRealtimeApp(Chat) {
    const app = Sloppy.create();
    app.realtime("/public/{roomId}", Chat, async (ctx) => {
        await ctx.accept();
        await ctx.send("ready", { roomId: ctx.params.roomId, userId: ctx.user?.sub ?? "anonymous" });
    });
    app.realtime("/rooms/{roomId}", Chat, async (ctx) => {
        await ctx.accept();
        await ctx.groups.join(`room:${ctx.params.roomId}`);
        await ctx.presence.set({ userId: ctx.user.sub });
        await ctx.send("ready", { roomId: ctx.params.roomId, userId: ctx.user.sub });
        ctx.on("sendMessage", async (input) => {
            await ctx.group(`room:${ctx.params.roomId}`).broadcast("messageCreated", {
                roomId: ctx.params.roomId,
                text: input.text,
            });
        });
        ctx.on("leaveRoom", async () => {
            await ctx.groups.leave(`room:${ctx.params.roomId}`);
        });
        ctx.on("who", async (input) => {
            const users = await ctx.presence.inGroup(`room:${input.roomId}`);
            await ctx.send("presenceList", { roomId: input.roomId, count: users.length });
        });
        ctx.on("typing", async (input) => {
            await ctx.group(`room:${input.roomId}`).broadcast("userTyping", {
                roomId: input.roomId,
                userId: ctx.user.sub,
            }, { exceptSelf: true });
        });
    }, { presence: true, validationFailurePolicy: "error" })
        .requiresAuth()
        .requiresScope("chat");
    app.websocket("/ws", async (socket) => {
        await socket.accept();
        for await (const message of socket.messages()) {
            if (message.kind === "text") {
                await socket.sendText(message.text);
            }
        }
    }, {
        protocols: ["sloppy.contract.websocket"],
        maxMessageBytes: 64,
        maxSendQueueBytes: 6,
        slowClientPolicy: "close",
    });
    app.websocket("/closing", async (socket) => {
        await socket.accept();
        await socket.close(1000, "contract complete");
    });
    app.websocket("/backpressure", async (socket) => {
        await socket.accept();
        for await (const message of socket.messages()) {
            if (message.kind === "text") {
                await socket.sendText("12345");
                await socket.sendText("67890");
            }
        }
    }, { maxSendQueueBytes: 6, slowClientPolicy: "close" });
    return app;
}

async function authRequiredCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        await expectRejectsStatus(host.realtime("/rooms/r1", Chat).connect(), 401);

        const publicClient = await host.realtime("/public/lobby", Chat).connect();
        await publicClient.expect("ready", { roomId: "lobby", userId: "" });
        await publicClient.close();

        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat", "chat:write"] })
            .connect();
        await alice.expect("ready", { roomId: "r1", userId: "alice" });
        await alice.send("who", { roomId: "r1" });
        await alice.expect("presenceList", { roomId: "r1", count: 1 });
        await alice.close();
    } finally {
        await host.close();
    }
}

async function groupJoinLeaveCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat"] })
            .connect();
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: ["chat"] })
            .connect();
        await alice.expect("ready", { roomId: "r1" });
        await bob.expect("ready", { roomId: "r1" });
        await bob.send("who", { roomId: "r1" });
        await bob.expect("presenceList", { roomId: "r1", count: 2 });
        await bob.send("leaveRoom", { roomId: "r1" });
        await bob.send("who", { roomId: "r1" });
        await bob.expect("presenceList", { roomId: "r1", count: 1 });
        await alice.send("sendMessage", { roomId: "r1", text: "after-leave" });
        await assert.rejects(
            () => bob.expect("messageCreated", undefined, { timeoutMs: 20 }),
            /waiting/u,
        );
        await alice.close();
        await bob.close();
    } finally {
        await host.close();
    }
}

async function broadcastScopeCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat"] })
            .connect();
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: ["chat"] })
            .connect();
        const mallory = await host.realtime("/rooms/r2", Chat)
            .asUser({ sub: "mallory", scopes: ["chat"] })
            .connect();
        await alice.expect("ready", { roomId: "r1" });
        await bob.expect("ready", { roomId: "r1" });
        await mallory.expect("ready", { roomId: "r2" });
        await alice.send("sendMessage", { roomId: "r1", text: "scoped" });
        await bob.expect("messageCreated", { roomId: "r1", text: "scoped" });
        await assert.rejects(
            () => mallory.expect("messageCreated", undefined, { timeoutMs: 20 }),
            /waiting/u,
        );
        await alice.close();
        await bob.close();
        await mallory.close();
    } finally {
        await host.close();
    }
}

async function presenceDisconnectCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat"] })
            .connect();
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: ["chat"] })
            .connect();
        await alice.expect("ready", { roomId: "r1" });
        await bob.expect("ready", { roomId: "r1" });
        await bob.send("who", { roomId: "r1" });
        await bob.expect("presenceList", { roomId: "r1", count: 2 });
        await alice.close();
        await bob.send("who", { roomId: "r1" });
        await bob.expect("presenceList", { roomId: "r1", count: 1 });
        await assertSocketCannotReceive(alice);
        await bob.close();
    } finally {
        await host.close();
    }
}

async function messageValidationCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const alice = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "alice", scopes: ["chat", "chat:write"] })
            .connect();
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: ["chat"] })
            .connect();
        await alice.expect("ready", { roomId: "r1" });
        await bob.expect("ready", { roomId: "r1" });
        assert.throws(
            () => bob.send("sendMessage", { roomId: "r1", text: "x".repeat(33) }),
            /Realtime message validation failed/u,
        );
        await bob.send("typing", { roomId: "r1" });
        await bob.expectError("SLOPPY_E_REALTIME_UNAUTHORIZED_EVENT");
        await alice.send("typing", { roomId: "r1" });
        await bob.expect("userTyping", { roomId: "r1", userId: "alice" });
        await bob.send("sendMessage", { roomId: "r1", text: "valid" });
        await bob.expect("messageCreated", { roomId: "r1", text: "valid" });
        await alice.close();
        await bob.close();
    } finally {
        await host.close();
    }
}

async function messageNoCrashCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const bob = await host.realtime("/rooms/r1", Chat)
            .asUser({ sub: "bob", scopes: ["chat"] })
            .connect();
        await bob.expect("ready", { roomId: "r1" });
        await bob._websocket.sendJson({ type: "missing", data: {} });
        await bob.expectError("SLOPPY_E_REALTIME_UNKNOWN_EVENT");
        await bob.send("sendMessage", { roomId: "r1", text: "still-open" });
        await bob.expect("messageCreated", { roomId: "r1", text: "still-open" });
        await bob.close();
    } finally {
        await host.close();
    }
}

async function backpressureLimitCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const ws = await host.websocket("/backpressure").connect();
        await ws.sendText("go");
        await new Promise((resolve) => setTimeout(resolve, 0));
        await ws.expectText("12345");
        await ws.expectClose(1013);
        assert.throws(
            () => ws.sendText("after-close"),
            /closed/u,
        );
    } finally {
        await host.close();
    }
}

async function websocketUpgradeRequiredCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        await host.get("/ws").expectProblem({
            status: 501,
            code: "SLOPPY_E_REALTIME_WEBSOCKET_UNAVAILABLE",
        });
        await expectRejectsStatus(
            host.websocket("/ws")
                .protocols(["wrong.protocol"])
                .connect(),
            400,
        );
        await host.post("/ws").expectStatus(405);
    } finally {
        await host.close();
    }
}

async function websocketCloseCodeCase() {
    const Chat = createChatChannel();
    const host = await TestHost.create(createRealtimeApp(Chat));
    try {
        const ws = await host.websocket("/closing").connect();
        await ws.expectClose(1000);
    } finally {
        await host.close();
    }
}

export async function runRealtimeContract({ tier }) {
    const startedAt = new Date().toISOString();
    const cases = [
        ["auth", "realtime.auth.required", authRequiredCase, "protected realtime endpoints reject unauthenticated connects and expose authenticated identity"],
        ["groups", "realtime.group.join-leave", groupJoinLeaveCase, "clients can join and leave realtime groups"],
        ["groups", "realtime.group.broadcast-scope", broadcastScopeCase, "group broadcasts reach members without leaking across rooms"],
        ["presence", "realtime.presence.disconnect", presenceDisconnectCase, "disconnect removes presence and stale clients cannot receive"],
        ["message", "realtime.message.validation", messageValidationCase, "valid realtime messages are accepted and invalid messages are rejected"],
        ["message", "realtime.message.no-crash", messageNoCrashCase, "unsupported realtime message types return stable errors without crashing"],
        ["lifecycle", "realtime.backpressure.limit", backpressureLimitCase, "WebSocket send queue limits close slow consumers predictably"],
        ["upgrade", "websocket.upgrade.required", websocketUpgradeRequiredCase, "WebSocket routes require upgrade semantics and protocol checks"],
        ["upgrade", "websocket.close-code", websocketCloseCodeCase, "WebSocket close code matches the current contract"],
    ];
    const findings = [];
    for (const [fixture, invariant, body, message] of cases) {
        findings.push(await runCase(fixture, invariant, body, message, { tier }));
    }
    return createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
}
