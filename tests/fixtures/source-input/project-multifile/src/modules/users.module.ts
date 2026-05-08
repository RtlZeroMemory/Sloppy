import { Results } from "sloppy";

export function usersModule(app) {
    const users = app.group("/users");

    users.get("/{id}", (ctx) => Results.json({
        id: ctx.route.id,
        name: "Ada Lovelace"
    })).withName("Users.Get");
}
