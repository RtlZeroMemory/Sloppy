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

async function seedUsers(db) {
    await db.exec("create table if not exists users (id integer primary key, name text not null, email text not null unique)");
    await db.exec(
        "insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)",
        [1, "Ada Lovelace", "ada@example.test", 1],
    );
    await db.exec(
        "insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)",
        [2, "Grace Hopper", "grace@example.test", 2],
    );
}

app.get("/users", async (db: Sqlite<"main">, ctx: RequestContext) => {
    await seedUsers(db);
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
        { deadline: ctx.deadline },
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: Sqlite<"main">,
    ctx: RequestContext,
) => {
    await seedUsers(db);
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = ?",
        [id],
        { deadline: ctx.deadline },
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: Sqlite<"main">,
    ctx: RequestContext,
) => {
    await seedUsers(db);
    await db.exec(
        "insert into users (name, email) values (?, ?)",
        [input.name, input.email],
        { deadline: ctx.deadline },
    );
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = last_insert_rowid()",
        [],
        { deadline: ctx.deadline },
    );
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
