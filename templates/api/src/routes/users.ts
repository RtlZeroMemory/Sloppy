import { Results } from "sloppy";
import { normalizeCreateUserRequest } from "../models/createUserRequest.ts";
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
        return user === null ? Results.notFound() : Results.ok(user);
    })
        .withName("Users.Get");

    app.post("/users", async (ctx) => Results.created(
        "/users/3",
        await createUser(db, normalizeCreateUserRequest(ctx.body.json())),
    )).withName("Users.Create");
}
