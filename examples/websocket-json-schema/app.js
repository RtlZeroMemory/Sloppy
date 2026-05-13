import { Sloppy, schema } from "sloppy";

const app = Sloppy.create();

const ClientMessage = schema.object({
    type: schema.enum(["ping", "echo"]),
    text: schema.string().optional(),
});

app.websocket("/json", async (socket) => {
    await socket.accept();

    for await (const message of socket.messages()) {
        const input = message.validate(ClientMessage);
        if (input.type === "ping") {
            await socket.sendJson({ type: "pong" });
        } else {
            await socket.sendJson({ type: "echo", text: input.text ?? "" });
        }
    }
}, {
    maxMessageBytes: 64 * 1024,
});

export default app;
