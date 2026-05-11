import { Results, Schema } from "sloppy";
import {
    createUser,
    getUser,
    listUsers,
} from "../services/usersService.ts";

export function usersModule(app) {
    const db = app.provider("sqlite:main");
    const CreateUser = Schema.object({
        name: Schema.string().min(1).max(100),
        email: Schema.string().email(),
    });
    const User = Schema.object({
        id: Schema.integer(),
        name: Schema.string(),
        email: Schema.string(),
    });

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
        const input = await ctx.body.validate(CreateUser);
        const user = await createUser(db, input);
        return Results.created(`/users/${user.id}`, user);
    })
        .accepts(CreateUser)
        .returns(User, { status: 201 })
        .withName("Users.Create");
}
