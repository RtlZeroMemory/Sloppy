import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();
const users = app.mapGroup("/users");

users.mapGet("/{id:int}", () => Results.json({ ok: true })).withName("Users.Get");
app.mapGet("/{tenant}/users/{id:int}", () => Results.text("tenant")).withName("Tenant.Users.Get");

export default app;
