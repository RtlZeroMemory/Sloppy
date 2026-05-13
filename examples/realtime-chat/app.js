import { Realtime, Sloppy, schema } from "sloppy";

export const Chat = Realtime.channel("chat", {
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

export default app;
