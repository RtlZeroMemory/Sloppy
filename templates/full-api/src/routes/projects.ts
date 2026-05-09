import { Results } from "sloppy";

export function projectsModule(app) {
    const projects = app.group("/projects");

    projects.get("/", () => Results.ok([
        { id: "p_sloppy", name: "Sloppy launch", status: "pre-alpha" }
    ])).withName("Projects.List");

    projects.post("/", () => Results.created("/projects/p_sloppy", {
        id: "p_sloppy",
        name: "Sloppy launch",
        status: "created"
    })).withName("Projects.Create");
}
