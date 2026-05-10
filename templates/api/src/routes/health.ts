import { Results } from "sloppy";
import { migrateUsers } from "../db/migrate.ts";

export function healthModule(app) {
    const db = app.provider("sqlite:main");

    app.get("/health", () => Results.text("ok"))
        .withName("Health");

    app.get("/health/live", () => Results.ok({ status: "healthy" }))
        .withName("Health.Live");

    app.get("/health/ready", async () => {
        await migrateUsers(db);
        const row = await db.queryOne("select 1 as ok", []);
        return row === null
            ? Results.status(503, { status: "unhealthy", checks: [{ name: "sqlite", status: "unhealthy" }] })
            : Results.ok({ status: "healthy", checks: [{ name: "sqlite", status: "healthy" }] });
    }).withName("Health.Ready");
}
