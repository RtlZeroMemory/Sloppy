import { Results } from "sloppy";
import { isUserName, normalizeName } from "validator-lite";

export function usersModule(app) {
    app.get("/users/{name}", (ctx) => {
        const name = normalizeName(ctx.route.name);
        return Results.ok({
            name,
            valid: isUserName(name),
        });
    }).withName("Users.Validate");
}
