import { Results, Sloppy } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
