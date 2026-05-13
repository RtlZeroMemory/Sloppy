import { Auth, Sloppy } from "sloppy";

const app = Sloppy.create();

app.use(Auth.jwtBearer({
    secret: "test-secret",
}));

app.websocket("/secure/ws", async (socket) => {
    const user = socket.ctx.requireUser();
    await socket.accept();
    await socket.sendJson({ type: "hello", sub: user.sub });
    await socket.close();
}).requiresAuth().requiresScope("realtime");

export default app;
