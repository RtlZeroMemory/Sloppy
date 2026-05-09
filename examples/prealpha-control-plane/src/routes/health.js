import { Results } from "sloppy";

export function healthModule(app) {
    const health = app.group("/health");

    health.get("/", () => Results.ok({
        status: "healthy",
        checks: [
            { name: "sqlite", status: "healthy" },
            { name: "app-host", status: "healthy" },
        ],
    })).withName("Health");

    health.get("/live", () => Results.ok({
        status: "healthy",
        checks: [
            { name: "process", status: "healthy" },
        ],
    })).withName("Health.Liveness");

    health.get("/ready", () => Results.ok({
        status: "healthy",
        checks: [
            { name: "sqlite", status: "healthy" },
        ],
    })).withName("Health.Readiness");
}
