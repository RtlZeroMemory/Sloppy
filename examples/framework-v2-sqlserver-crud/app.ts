import {
    Body,
    Email,
    NonEmptyString,
    PositiveInt,
    RequestContext,
    Results,
    Route,
    Sloppy,
} from "sloppy";
import { SqlServer } from "sloppy/providers/sqlserver";

type UserCreate = {
    name: NonEmptyString;
    email: Email;
};

type UserDto = {
    id: number;
    name: string;
    email: string;
};

const app = Sloppy.create();

app.get("/users", async (db: SqlServer<"main">, ctx: RequestContext) => {
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
        { deadline: ctx.deadline },
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: SqlServer<"main">,
    ctx: RequestContext,
) => {
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = ?",
        [id],
        { deadline: ctx.deadline },
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: SqlServer<"main">,
    ctx: RequestContext,
) => {
    await db.exec(
        "insert into users (name, email) values (?, ?)",
        [input.name, input.email],
        { deadline: ctx.deadline },
    );
    const user = await db.queryOne<UserDto>(
        "select top 1 id, name, email from users where email = ? order by id desc",
        [input.email],
        { deadline: ctx.deadline },
    );
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
