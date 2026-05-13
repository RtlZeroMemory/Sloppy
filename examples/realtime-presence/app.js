import { Realtime, Sloppy, schema } from "sloppy";

export const PresenceChannel = Realtime.channel("presence", {
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

export default app;
