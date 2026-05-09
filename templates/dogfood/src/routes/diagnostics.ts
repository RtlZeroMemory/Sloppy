import { Results } from "sloppy";

export function diagnosticsModule(app) {
    const diagnostics = app.group("/diagnostics");

    diagnostics.get("/summary", () => Results.ok({
        routes: 8,
        mode: "template",
        runtime: "pre-alpha"
    })).withName("Diagnostics.Summary");
}
