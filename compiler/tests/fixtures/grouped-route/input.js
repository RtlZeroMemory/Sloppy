import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const users = app.mapGroup("/users");

users.mapGet("/{id:int}", () => Results.json({ ok: true })).withName("Users.Get");

export default app;
