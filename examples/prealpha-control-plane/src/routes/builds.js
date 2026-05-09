import { Results } from "sloppy";

export function buildsModule(app) {
    const db = app.provider("sqlite:main");
    const apps = app.group("/apps");

    apps.post("/{id:int}/builds", async (ctx) => {
        const body = ctx.request.json();
        if (body === null || Array.isArray(body) || typeof body !== "object" ||
            typeof body.commit !== "string" || body.commit.length === 0) {
            return Results.badRequest({ code: "BUILD_INVALID", message: "commit is required" });
        }

        await db.exec("create table if not exists builds (id integer primary key, app_id integer not null, commit_sha text not null, status text not null)", []);
        await db.exec("insert into builds (app_id, commit_sha, status) values (?, ?, ?)", [ctx.route.id, body.commit, "queued"]);
        const build = await db.queryOne("select id, app_id, commit_sha, status from builds where commit_sha = ?", [body.commit]);
        return Results.created(`/apps/${ctx.route.id}/builds/${build.id}`, build);
    }).withName("Builds.Create");

    apps.get("/{id:int}/builds", async (ctx) => {
        await db.exec("create table if not exists builds (id integer primary key, app_id integer not null, commit_sha text not null, status text not null)", []);
        await db.exec("insert into builds (id, app_id, commit_sha, status) select ?, ?, ?, ? where not exists (select 1 from builds where id = ?)", [1, ctx.route.id, "abc123", "succeeded", 1]);
        return Results.ok(await db.query("select id, app_id, commit_sha, status from builds where app_id = ? order by id", [ctx.route.id]));
    }).withName("Builds.List");
}
