import { Results } from "sloppy";

export function healthModule(app) {
    const health = app.group("/health");

    health.get("/", () => Results.text("ok")).withName("Health.Get");
}

export function usersModule(app) {
    const users = app.group("/users");

    users.get("/", () => Results.json([{ id: "demo", name: "Demo User" }]))
        .withName("Users.List");
    users.get("/{id}", (ctx) => Results.json({ id: ctx.route.id, name: "Demo User" }))
        .withName("Users.Get");
}
