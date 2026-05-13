import { Sloppy, schema } from "sloppy";

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
            } else {
                await socket.sendJson({ type: "echo", text: input.text ?? "" });
            }
        }
    }
}, {
    origins: ["https://app.example.com"],
    protocols: ["sloppy.realtime"],
    maxMessageBytes: 64 * 1024,
    maxSendQueueBytes: 1024 * 1024,
    heartbeatMs: 15_000,
    idleTimeoutMs: 30_000,
});

export default app;
