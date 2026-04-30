import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/users", () => Results.json([{ id: 1 }])).withName("Users.List");
app.mapPost("/users", () => Results.json({ id: 2 })).withName("Users.Create");
app.mapPut("/users/{id:int}", (ctx) => Results.json({ id: ctx.route.id, updated: true })).withName("Users.Update");
app.mapPatch("/users/{id:int}", (ctx) => Results.json({ id: ctx.route.id, patched: true })).withName("Users.Patch");
app.mapDelete("/users/{id:int}", () => Results.noContent()).withName("Users.Delete");

export default app;
