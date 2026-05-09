import { Results } from "sloppy";

export function appsModule(app) {
    const apps = app.group("/apps");

    apps.get("/", () => Results.ok([
        { id: "api", projectId: "sloppy", name: "prealpha-api", environment: "dev" }
    ])).withName("Apps.List");

    apps.get("/{id}", (ctx) => Results.ok({
        id: ctx.route.id,
        projectId: "sloppy",
        name: "prealpha-api",
        environment: "dev"
    })).withName("Apps.Get");
}
