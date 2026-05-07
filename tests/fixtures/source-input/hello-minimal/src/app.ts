import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
