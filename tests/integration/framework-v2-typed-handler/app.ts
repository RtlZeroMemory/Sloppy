import { Sloppy, Results, RequestContext, Query, Route } from "sloppy";

const app = Sloppy.create();

app.get("/framework/:id", (
    id: Route<number>,
    active: Query<boolean>,
    ctx: RequestContext,
) => {
    return Results.ok({
        id,
        active,
        method: ctx.request.method,
        path: ctx.request.path,
    });
});

export default app;
