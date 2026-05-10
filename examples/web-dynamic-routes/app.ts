import { Results, Sloppy } from "sloppy";
import { Environment } from "sloppy/os";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");

const routes = [
    ["/hello/Ada", () => Results.json({ hello: "Ada" })],
    ["/hello/Grace", () => Results.json({ hello: "Grace" })],
] as const;

for (const [path, handler] of routes) {
    app.get(path, handler);
}

function getStatusPath() {
    return "/status";
}

app.get(getStatusPath(), () => Results.status(Date.now() % 2 === 0 ? 200 : 201));

if (Environment.get("ENABLE_ADMIN") === "true") {
    app.get("/admin/status", () => Results.text("admin ok"));
}

export default app;
