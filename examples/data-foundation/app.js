import { Sloppy, data, sql } from "sloppy";

export const DataModule = Sloppy.module("data")
    .capabilities((caps) => {
        caps.addDatabase("data.main", {
            provider: "sqlite",
            access: "readwrite",
        });
    })
    .services((services) => {
        services.addSingleton("data.main", () => data.createFakeProvider({
            query(lowered) {
                return [{
                    id: lowered.parameters[0] ?? "demo",
                    loweredText: lowered.text,
                }];
            },
            exec(lowered) {
                return {
                    affectedRows: 1,
                    loweredText: lowered.text,
                };
            },
        }));
    });

export const UsersModule = Sloppy.module("users")
    .dependsOn("data")
    .routes((app) => {
        app.mapGroup("/users")
            .withTags("Users")
            .mapGet("/{id:int}", async ({ route, services }) => {
                const db = services.get("data.main");
                const user = await db.queryOne`
                    select id, name
                    from users
                    where id = ${route.id ?? "demo"}
                `;

                await db.transaction(async (tx) => {
                    await tx.exec`
                        insert into audit_log (user_id)
                        values (${route.id ?? "demo"})
                    `;
                });

                return user;
            })
            .withName("Users.Get");
    });

const lowered = sql`select id from users where id = ${1}`;

const builder = Sloppy.createBuilder();

builder
    .addModule(UsersModule)
    .addModule(DataModule);

const app = builder.build();

export { lowered };
export default app;
