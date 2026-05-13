import { Realtime, Sloppy, TestHost, schema } from "sloppy";

const Echo = Realtime.channel("echo", {
    client: {
        echo: schema.object({ text: schema.string() }),
    },
    server: {
        echoed: schema.object({ text: schema.string() }),
    },
});

const app = Sloppy.create();

app.realtime("/echo", Echo, async (ctx) => {
    await ctx.accept();
    ctx.on("echo", async (input) => {
        await ctx.send("echoed", input);
    });
});

const host = await TestHost.create(app);

try {
    const client = await host.realtime("/echo", Echo).connect();

    await client.send("echo", { text: "hello" });
    await client.expect("echoed", { text: "hello" });
    await client.close();
} finally {
    await host.close();
}
