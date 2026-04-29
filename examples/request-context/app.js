import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/users/{id:int}", (ctx) => {
    return Results.json({
        id: ctx.route.id,
        q: ctx.query.q,
        method: ctx.request.method,
        path: ctx.request.path,
        rawTarget: ctx.request.rawTarget,
    });
}).withName("Users.Get");

export default app;
