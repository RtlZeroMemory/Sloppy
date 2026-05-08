import { Sloppy, Results, RequestContext, Route } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");

app.get("/hello/{name}", (
    name: Route<string>,
    ctx: RequestContext,
) => Results.ok({
    hello: name,
    method: ctx.request.method,
    path: ctx.request.path,
})).withName("Hello.Get");

export default app;
