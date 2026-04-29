import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/users/{id:int}", ({ route, query, request }) => {
    return Results.json({
        id: route.id,
        q: query.q,
        path: request.path,
    });
}).withName("Users.Get");

export default app;
