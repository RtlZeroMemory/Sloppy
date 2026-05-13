import { Realtime, Sloppy, TestHost, schema } from "sloppy";

const SecureChannel = Realtime.channel("secureChat", {
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

const host = await TestHost.create(app);

try {
    await host.realtime("/secure/chat", SecureChannel)
        .origin("https://app.example.com")
        .connect()
        .expectRejected(401);

    const writer = await host.realtime("/secure/chat", SecureChannel)
        .origin("https://app.example.com")
        .asUser({ sub: "writer", scopes: ["chat"], roles: ["admin"] })
        .connect();

    await writer.send("adminMessage", { text: "approved" });
    await writer.expect("accepted", { text: "approved" });
    await writer.close();
} finally {
    await host.close();
}
