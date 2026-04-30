import { Sloppy, data, Results, sql } from "../../stdlib/sloppy/index.js";

export const SqliteModule = Sloppy.module("data.sqlite")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "sqlite",
            database: ":memory:",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () => data.sqlite.open({
            database: ":memory:",
            capability: "data.main",
            access: "readwrite",
        }));
    });

export const UsersModule = Sloppy.module("users")
    .dependsOn("data.sqlite")
    .routes((app) => {
        app.mapGet("/users/{id:int}", async ({ route, services }) => {
            const db = services.get("data.main");

            await db.exec`
                create table if not exists users (id integer primary key, name text not null)
            `;

            await db.transaction(async (tx) => {
                await tx.exec`
                    insert or ignore into users (id, name) values (${1}, ${"Ada"})
                `;
            });

            const user = await db.queryOne`
                select id, name
                from users
                where id = ${route.id ?? 1}
            `;

            return user ? Results.ok(user) : Results.notFound();
        })
            .withName("Users.Get");
    });

export const lowered = sql`select name from users where id = ${1}`;

const builder = Sloppy.createBuilder();

builder
    .addModule(SqliteModule)
    .addModule(UsersModule);

const app = builder.build();

export default app;
