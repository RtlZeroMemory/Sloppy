import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();

builder.capabilities.addDatabase("data.main", {
    provider: "sqlserver",
    access: "readwrite",
    database: "sloppy_framework_v2",
});

const app = builder.build();
const db = app.provider("sqlserver:main");

app.get("/users", async (ctx) => {
    const rows = await db.query(
        "select id, name, email from users order by id",
        [],
        { deadline: ctx.deadline },
    );
    return Results.json(rows);
}).withName("Users.List");

app.get("/users/{id:int}", async (ctx) => {
    const user = await db.queryOne(
        "select id, name, email from users where id = ?",
        [ctx.route.id],
        { deadline: ctx.deadline },
    );
    return user === null ? Results.notFound({ error: "user_not_found" }) : Results.json(user);
}).withName("Users.Get");

export default app;
