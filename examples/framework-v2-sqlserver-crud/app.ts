import {
    Body,
    Email,
    NonEmptyString,
    PositiveInt,
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

app.get("/users", async (db: SqlServer<"main">) => {
    const users = await db.query<UserDto>(
        "select id, name, email from users order by id",
        [],
    );
    return Results.ok(users);
}).withName("Users.List");

app.get("/users/{id:int}", async (
    id: Route<PositiveInt>,
    db: SqlServer<"main">,
) => {
    const user = await db.queryOne<UserDto>(
        "select id, name, email from users where id = ?",
        [id],
    );
    return user === null ? Results.notFound() : Results.ok(user);
}).withName("Users.Get");

app.post("/users", async (
    input: Body<UserCreate>,
    db: SqlServer<"main">,
) => {
    const user = await db.queryOne<UserDto>(
        "insert into users (name, email) output inserted.id, inserted.name, inserted.email values (?, ?)",
        [input.name, input.email],
    );
    if (user === null) {
        throw new Error("SQL Server user insert did not return a row.");
    }
    return Results.created(`/users/${user.id}`, user);
}).withName("Users.Create");

export default app;
