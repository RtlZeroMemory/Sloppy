import { Realtime, Sloppy, schema } from "sloppy";

export const SecureChannel = Realtime.channel("secureChat", {
    client: {
        adminMessage: Realtime.event(schema.object({
            text: schema.string(),
        })).requiresRole("admin"),
    },
    server: {
        accepted: schema.object({
            text: schema.string(),
        }),
    },
});

const app = Sloppy.create();

app.realtime("/secure/chat", SecureChannel, async (ctx) => {
    await ctx.accept();
    ctx.on("adminMessage", async (input) => {
        await ctx.send("accepted", { text: input.text });
    });
}).requiresAuth().requiresScope("chat").allowedOrigins(["https://app.example.com"]);

export default app;
