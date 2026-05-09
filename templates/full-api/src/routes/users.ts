import { Results } from "sloppy";

export function usersModule(app) {
    const users = app.group("/users");

    users.get("/", () => Results.ok([
        { id: "u_ada", name: "Ada Lovelace", role: "admin" },
        { id: "u_grace", name: "Grace Hopper", role: "operator" }
    ])).withName("Users.List");

    users.get("/{id}", (ctx) => Results.ok({
        id: ctx.route.id,
        name: "Ada Lovelace",
        role: "admin"
    })).withName("Users.Get");
}
