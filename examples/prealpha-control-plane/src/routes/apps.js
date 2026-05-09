import { Results } from "sloppy";

export function appsModule(app) {
    const db = app.provider("sqlite:main");
    const apps = app.group("/apps");

    apps.get("/", async () => {
        await db.exec("create table if not exists apps (id integer primary key, project_id integer not null, name text not null, environment text not null)", []);
        await db.exec("insert into apps (id, project_id, name, environment) select ?, ?, ?, ? where not exists (select 1 from apps where id = ?)", [1, 1, "compiler-api", "Development", 1]);
        await db.exec("insert into apps (id, project_id, name, environment) select ?, ?, ?, ? where not exists (select 1 from apps where id = ?)", [2, 2, "control-plane", "Development", 2]);
        return Results.ok(await db.query("select id, project_id, name, environment from apps order by id", []));
    }).withName("Apps.List");

    apps.post("/", async (ctx) => {
        const body = ctx.request.json();
        if (body === null || Array.isArray(body) || typeof body !== "object" ||
            typeof body.name !== "string" || body.name.length === 0 ||
            !Number.isInteger(body.projectId) || body.projectId <= 0) {
            return Results.badRequest({ code: "APP_INVALID", message: "projectId and name are required" });
        }

        await db.exec("create table if not exists apps (id integer primary key, project_id integer not null, name text not null, environment text not null)", []);
        await db.exec("insert into apps (project_id, name, environment) values (?, ?, ?)", [body.projectId, body.name, body.environment ?? "Development"]);
        const created = await db.queryOne("select id, project_id, name, environment from apps where id = last_insert_rowid()", []);
        return Results.created(`/apps/${created.id}`, created);
    }).withName("Apps.Create");

    apps.get("/{id:int}", async (ctx) => {
        await db.exec("create table if not exists apps (id integer primary key, project_id integer not null, name text not null, environment text not null)", []);
        await db.exec("insert into apps (id, project_id, name, environment) select ?, ?, ?, ? where not exists (select 1 from apps where id = ?)", [1, 1, "compiler-api", "Development", 1]);
        const current = await db.queryOne("select id, project_id, name, environment from apps where id = ?", [ctx.route.id]);
        return current === null ? Results.notFound({ code: "APP_NOT_FOUND" }) : Results.ok(current);
    }).withName("Apps.Get");
}
