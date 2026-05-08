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
import { Sqlite } from "sloppy/providers/sqlite";

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

async function seedUsers(db, ctx) {
    await db.exec(
        "create table if not exists users (id integer primary key, name text not null, email text not null unique)",
        [],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    await db.exec(
        "insert or ignore into users (id, name, email) values (?, ?, ?)",
        [1, "Ada Lovelace", "ada@example.test"],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    await db.exec(
        "insert or ignore into users (id, name, email) values (?, ?, ?)",
        [2, "Grace Hopper", "grace@example.test"],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
}

app.get("/users", async (db: Sqlite<"main">, ctx: RequestContext) => {
    await seedUsers(db, ctx);
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: Sqlite<"main">,
    ctx: RequestContext,
) => {
    await seedUsers(db, ctx);
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = ?",
        [id],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: Sqlite<"main">,
    ctx: RequestContext,
) => {
    await seedUsers(db, ctx);
    await db.exec(
        "insert or ignore into users (name, email) values (?, ?)",
        [input.name, input.email],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where email = ?",
        [input.email],
        { signal: ctx.signal, deadline: ctx.deadline },
    );
    if (user === null) {
        throw new Error("SQLite user insert did not return a row.");
    }
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
