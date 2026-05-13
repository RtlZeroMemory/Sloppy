import assert from "node:assert/strict";

import { Sloppy, TestHost, schema } from "sloppy";

const app = Sloppy.create();

const ClientMessage = schema.object({
    type: schema.enum(["ping", "echo"]),
    text: schema.string().optional(),
});

app.websocket("/ws", async (socket) => {
    await socket.accept();

    for await (const message of socket.messages()) {
        if (message.kind === "text") {
            await socket.sendText(`echo:${message.text}`);
            continue;
        }
        if (message.kind === "json") {
            const input = message.validate(ClientMessage);
            if (input.type === "ping") {
                await socket.sendJson({ type: "pong" });
            }
        }
    }
}, {
    origins: ["https://app.example.com"],
    protocols: ["sloppy.realtime"],
});

const host = await TestHost.create(app);

try {
    const ws = await host.websocket("/ws")
        .origin("https://app.example.com")
        .protocols(["sloppy.realtime"])
        .connect();

    assert.equal(ws.protocol, "sloppy.realtime");

    await ws.sendText("hello");
    await ws.expectText("echo:hello");
    await ws.sendJson({ type: "ping" });
    await ws.expectJson({ type: "pong" });
    await ws.close();
} finally {
    await host.close();
}
