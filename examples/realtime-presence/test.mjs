import { Realtime, Sloppy, TestHost, schema } from "sloppy";

const PresenceChannel = Realtime.channel("presence", {
    client: {
        setStatus: schema.object({
            status: schema.string(),
        }),
        list: schema.object({}),
    },
    server: {
        presenceCount: schema.object({
            count: schema.int(),
        }),
    },
});

const app = Sloppy.create();

app.realtime("/presence/{roomId}", PresenceChannel, async (ctx) => {
    await ctx.accept();
    await ctx.groups.join(`room:${ctx.params.roomId}`);
    ctx.on("setStatus", async (input) => {
        await ctx.presence.set({ metadata: { status: input.status } });
    });
    ctx.on("list", async () => {
        const users = await ctx.presence.inGroup(`room:${ctx.params.roomId}`);
        await ctx.send("presenceCount", { count: users.length });
    });
}, { presence: true }).requiresAuth();

const host = await TestHost.create(app);

try {
    const alice = await host.realtime("/presence/r1", PresenceChannel)
        .asUser({ sub: "alice" })
        .connect();
    const bob = await host.realtime("/presence/r1", PresenceChannel)
        .asUser({ sub: "bob" })
        .connect();

    await alice.send("setStatus", { status: "online" });
    await bob.send("setStatus", { status: "online" });
    await bob.send("list", {});
    await bob.expect("presenceCount", { count: 2 });

    await alice.close();
    await bob.close();
} finally {
    await host.close();
}
