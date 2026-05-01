import { Results } from "sloppy";

export function usersModule(app) {
    const db = app.provider("sqlite:main");
    const users = app.group("/users");

    users.get("/", async () => {
        return Results.json(await db.query("select id, name, email from users", []));
    });
}
