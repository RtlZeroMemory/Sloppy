import {
    Body,
    Email,
    NonEmptyString,
    PositiveInt,
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
    await db.exec(
        "create table if not exists users (id integer primary key, name text not null, email text not null unique)",
        [],
    );
    await db.exec(
        "insert or ignore into users (id, name, email) values (?, ?, ?)",
        [1, "Ada Lovelace", "ada@example.test"],
    );
    await db.exec(
        "insert or ignore into users (id, name, email) values (?, ?, ?)",
        [2, "Grace Hopper", "grace@example.test"],
    );
}

app.get("/users", async (db: Sqlite<"main">) => {
    await seedUsers(db);
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: Sqlite<"main">,
) => {
    await seedUsers(db);
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = ?",
        [id],
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: Sqlite<"main">,
) => {
    await seedUsers(db);
    await db.exec(
        "insert or ignore into users (name, email) values (?, ?)",
        [input.name, input.email],
    );
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where email = ?",
        [input.email],
    );
    if (user === null) {
        throw new Error("SQLite user insert did not return a row.");
    }
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
