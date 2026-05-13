import { Results } from "sloppy";

export function deploymentsModule(app) {
    const db = app.provider("sqlite:main");
    const apps = app.group("/apps");

    apps.post("/{id:int}/deployments", async (ctx) => {
        const body = ctx.request.json();
        if (body === null || Array.isArray(body) || typeof body !== "object" ||
            !Number.isInteger(body.buildId) || body.buildId <= 0) {
            return Results.badRequest({ code: "DEPLOYMENT_INVALID", message: "buildId is required" });
        }

        await db.exec("create table if not exists deployments (id integer primary key, app_id integer not null, build_id integer not null, status text not null)", []);
        await db.exec("insert into deployments (app_id, build_id, status) values (?, ?, ?)", [ctx.route.id, body.buildId, "started"]);
        const deployment = await db.queryOne("select id, app_id, build_id, status from deployments where id = last_insert_rowid()", []);
        return Results.created(`/apps/${ctx.route.id}/deployments/${deployment.id}`, deployment);
    }).withName("Deployments.Create");
}
