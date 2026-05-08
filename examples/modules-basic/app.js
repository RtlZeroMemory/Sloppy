import { Sloppy, Results } from "sloppy";

export const DataModule = Sloppy.module("data")
    .metadata("description", "in-memory demo data service")
    .services((services) => {
        services.addSingleton("data.users", () => new Map([
            ["demo", { id: "demo", name: "Demo User" }],
        ]));
    });

export const UsersModule = Sloppy.module("users")
    .dependsOn("data")
    .metadata("area", "users")
    .services((services) => {
        services.addSingleton("users.message", () => "hello from a module");
    })
    .routes((app) => {
        app.mapGroup("/users")
            .withTags("Users")
            .mapGet("/{id:int}", ({ route, services }) => {
                const users = services.get("data.users");
                const id = route.id ?? "demo";

                return Results.ok({
                    ...users.get(id),
                    id,
                    message: services.get("users.message"),
                });
            })
            .withName("Users.Get");
    });

const builder = Sloppy.createBuilder();

builder
    .addModule(UsersModule)
    .addModule(DataModule);

const app = builder.build();

export default app;
