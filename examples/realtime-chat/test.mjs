import assert from "node:assert/strict";

import { Realtime, Sloppy, TestHost, schema } from "sloppy";

const Chat = Realtime.channel("chat", {
    client: {
        sendMessage: schema.object({
            roomId: schema.string(),
            text: schema.string().maxLength(1000),
        }),
    },
    server: {
        messageCreated: schema.object({
            roomId: schema.string(),
            text: schema.string(),
        }),
    },
});

const app = Sloppy.create();

app.realtime("/rooms/{roomId}", Chat, async (ctx) => {
    await ctx.accept();
    await ctx.groups.join(`room:${ctx.params.roomId}`);
    ctx.on("sendMessage", async (input) => {
        await ctx.group(`room:${ctx.params.roomId}`).broadcast("messageCreated", {
            roomId: ctx.params.roomId,
            text: input.text,
        });
    });
}).requiresAuth().requiresScope("chat");

const host = await TestHost.create(app);

try {
    const alice = await host.realtime("/rooms/r1", Chat)
        .asUser({ sub: "alice", scopes: ["chat"] })
        .connect();
    const bob = await host.realtime("/rooms/r1", Chat)
        .asUser({ sub: "bob", scopes: ["chat"] })
        .connect();

    await new Promise((resolve) => setTimeout(resolve, 0));
    await alice.send("sendMessage", { roomId: "r1", text: "hello" });
    await bob.expect("messageCreated", { roomId: "r1", text: "hello" });

    assert.equal(bob.closed, false);
    await alice.close();
    await bob.close();
} finally {
    await host.close();
}
