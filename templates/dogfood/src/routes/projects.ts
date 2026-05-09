import { Results } from "sloppy";

export function projectsModule(app) {
    const projects = app.group("/projects");

    projects.get("/", () => Results.ok([
        { id: "sloppy", name: "Sloppy runtime", owner: "runtime" }
    ])).withName("Projects.List");

    projects.get("/{id}", (ctx) => Results.ok({
        id: ctx.route.id,
        name: "Sloppy runtime",
        owner: "runtime"
    })).withName("Projects.Get");
}
