import { Results } from "sloppy";

export function usersModule(app) {
    const db = app.provider("sqlite:main");
    const users = app.group("/users");

    users.get("/", () => {
        db.exec("create table if not exists users (id integer primary key, name text not null, email text not null unique)", []);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [1, "Ada Lovelace", "ada@example.test", 1]);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [2, "Grace Hopper", "grace@example.test", 2]);
        return Results.json(db.query("select id, name, email from users order by id", []));
    }).withName("Users.List");

    users.get("/{id:int}", (ctx) => {
        db.exec("create table if not exists users (id integer primary key, name text not null, email text not null unique)", []);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [1, "Ada Lovelace", "ada@example.test", 1]);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [2, "Grace Hopper", "grace@example.test", 2]);
        const user = db.queryOne("select id, name, email from users where id = ?", [ctx.route.id]);
        return user === null ? Results.notFound({ error: "user_not_found" }) : Results.json(user);
    }).withName("Users.Get");

    users.post("/", (ctx) => {
        const body = ctx.request.json();
        if (body === null || Array.isArray(body) || typeof body !== "object" ||
            typeof body.name !== "string" || body.name.length === 0 ||
            typeof body.email !== "string" || body.email.length === 0) {
            return Results.problem(
                { title: "Invalid user payload", detail: "name and email are required" },
                { status: 400 },
            );
        }

        db.exec("create table if not exists users (id integer primary key, name text not null, email text not null unique)", []);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [1, "Ada Lovelace", "ada@example.test", 1]);
        db.exec("insert into users (id, name, email) select ?, ?, ? where not exists (select 1 from users where id = ?)", [2, "Grace Hopper", "grace@example.test", 2]);
        db.exec("insert into users (name, email) values (?, ?)", [body.name, body.email]);
        const user = db.queryOne("select id, name, email from users where id = last_insert_rowid()", []);
        return Results.created(`/users/${user.id}`, user);
    }).withName("Users.Create");
}
