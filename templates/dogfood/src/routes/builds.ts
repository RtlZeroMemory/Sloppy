import { Results } from "sloppy";

export function buildsModule(app) {
    const apps = app.group("/apps");

    apps.get("/{id}/builds", (ctx) => Results.ok([
        { id: "b_001", appId: ctx.route.id, commit: "local", status: "passed" }
    ])).withName("Builds.List");

    apps.post("/{id}/builds", (ctx) => Results.created(`/apps/${ctx.route.id}/builds/b_002`, {
        id: "b_002",
        appId: ctx.route.id,
        commit: "local",
        status: "queued"
    })).withName("Builds.Create");
}
