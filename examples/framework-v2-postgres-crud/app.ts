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
import { Postgres } from "sloppy/providers/postgres";

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

app.get("/users", async (db: Postgres<"main">, ctx: RequestContext) => {
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
        { deadline: ctx.deadline },
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: Postgres<"main">,
    ctx: RequestContext,
) => {
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = $1",
        [id],
        { deadline: ctx.deadline },
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: Postgres<"main">,
    ctx: RequestContext,
) => {
    const user = await db.queryOne<UserDto>(
        "insert into users (name, email) values ($1, $2) returning id, name, email",
        [input.name, input.email],
        { deadline: ctx.deadline },
    );
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
