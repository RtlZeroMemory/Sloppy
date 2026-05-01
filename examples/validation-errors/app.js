import { Sloppy, Results, schema } from "sloppy";

const UserCreate = schema.object({
    name: schema.string().min(1),
    email: schema.string().email(),
});

const app = Sloppy.create();

app.post("/users", (ctx) => Results.created("/users/1", {
    user: ctx.body.json(UserCreate),
})).withName("Users.Create");

export default app;
