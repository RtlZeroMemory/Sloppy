import { RateLimit, Sloppy } from "sloppy";

const app = Sloppy.create();

app.websocket("/ws", async (socket) => {
  await socket.accept();
  await socket.sendText("connected");
})
  .rateLimit(RateLimit.fixedWindow({
    name: "ws-connect",
    limit: 10,
    windowMs: 60_000,
    partitionBy: RateLimit.partition.ip(),
  }));

export default app;
