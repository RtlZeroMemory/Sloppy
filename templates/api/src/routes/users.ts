import { Results } from "sloppy";
import {
    createUserValidationErrors,
    normalizeCreateUserRequest,
} from "../models/createUserRequest.ts";
import {
    createUser,
    getUser,
    listUsers,
} from "../services/usersService.ts";

export function usersModule(app) {
    const db = app.provider("sqlite:main");

    app.get("/users", async () => Results.ok(await listUsers(db)))
        .withName("Users.List");

    app.get("/users/{id:int}", async (ctx) => {
        const user = await getUser(db, ctx.route.id);
        if (user === null) {
            return Results.notFound();
        }
        return Results.ok(user);
    })
        .withName("Users.Get");

    app.post("/users", async (ctx) => {
        const input = normalizeCreateUserRequest(ctx.body.json());
        const errors = createUserValidationErrors(input);
        if (Object.keys(errors).length > 0) {
            return Results.badRequest({
                title: "Validation failed",
                status: 400,
                errors,
            });
        }
        const user = await createUser(db, input);
        return Results.created(`/users/${user.id}`, user);
    }).withName("Users.Create");
}
