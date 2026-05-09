import { Results } from "sloppy";

export function projectsModule(app) {
    const db = app.provider("sqlite:main");
    const projects = app.group("/projects");

    projects.get("/", async (ctx) => {
        await db.exec("create table if not exists projects (id integer primary key, slug text not null unique, name text not null, owner text not null)", []);
        await db.exec("insert into projects (id, slug, name, owner) select ?, ?, ?, ? where not exists (select 1 from projects where id = ?)", [1, "compiler", "Compiler Platform", "runtime", 1]);
        await db.exec("insert into projects (id, slug, name, owner) select ?, ?, ?, ? where not exists (select 1 from projects where id = ?)", [2, "web", "Web Framework", "framework", 2]);
        const owner = ctx.query.owner;
        if (owner !== undefined) {
            return Results.ok(await db.query("select id, slug, name, owner from projects where owner = ? order by id", [owner]));
        }
        return Results.ok(await db.query("select id, slug, name, owner from projects order by id", []));
    }).withName("Projects.List");

    projects.post("/", async (ctx) => {
        const body = ctx.request.json();
        if (body === null || Array.isArray(body) || typeof body !== "object" ||
            typeof body.slug !== "string" || body.slug.length === 0 ||
            typeof body.name !== "string" || body.name.length === 0) {
            return Results.badRequest({ code: "PROJECT_INVALID", message: "slug and name are required" });
        }

        await db.exec("create table if not exists projects (id integer primary key, slug text not null unique, name text not null, owner text not null)", []);
        await db.exec("insert into projects (slug, name, owner) values (?, ?, ?)", [body.slug, body.name, body.owner ?? "platform"]);
        const project = await db.queryOne("select id, slug, name, owner from projects where slug = ?", [body.slug]);
        return Results.created(`/projects/${project.id}`, project);
    }).withName("Projects.Create");

    projects.get("/{id:int}", async (ctx) => {
        await db.exec("create table if not exists projects (id integer primary key, slug text not null unique, name text not null, owner text not null)", []);
        await db.exec("insert into projects (id, slug, name, owner) select ?, ?, ?, ? where not exists (select 1 from projects where id = ?)", [1, "compiler", "Compiler Platform", "runtime", 1]);
        const project = await db.queryOne("select id, slug, name, owner from projects where id = ?", [ctx.route.id]);
        return project === null ? Results.notFound({ code: "PROJECT_NOT_FOUND" }) : Results.ok(project);
    }).withName("Projects.Get");
}
