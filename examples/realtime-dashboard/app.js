import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/", () => Results.html(`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Sloppy Realtime Dashboard</title>
</head>
<body>
  <h1>Sloppy Realtime Dashboard</h1>
  <pre id="events"></pre>
  <script>
    const output = document.getElementById("events");
    const events = new EventSource("/events");
    events.addEventListener("status", (event) => {
      output.textContent += event.data + "\\n";
    });
  </script>
</body>
</html>`));

app.sse("/events", async (ctx, stream) => {
    stream.comment("dashboard connected");
    stream.event("status", {
        path: ctx.request.path,
        state: "ready",
    });
    stream.heartbeat();
});

app.ws("/socket", async (ctx, socket) => {
    await socket.sendJson({ state: "ready" });
});

export default app;
