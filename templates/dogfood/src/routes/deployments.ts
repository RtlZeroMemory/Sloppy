import { Results } from "sloppy";

export function deploymentsModule(app) {
    const deployments = app.group("/deployments");

    deployments.get("/", () => Results.ok([
        { id: "d_001", buildId: "b_001", target: "local", status: "active" }
    ])).withName("Deployments.List");
}
